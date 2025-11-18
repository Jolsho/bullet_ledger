/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

use std::{io, os::fd::{AsRawFd, OwnedFd}, sync::{atomic::{AtomicUsize, Ordering}, Arc}};
use std::cell::UnsafeCell;

use mio::{event::Source, unix::SourceFd};
use nix::{sys::eventfd::{EfdFlags, EventFd}, unistd};

pub trait Msg {
    fn new(default_cap: Option<usize>) -> Self;
}

pub struct SpscQueue<T> {
    buffer: Vec<UnsafeCell<Option<T>>>,
    capacity: usize,
    head: AtomicUsize,
    tail: AtomicUsize,
}

unsafe impl<T: Send> Send for SpscQueue<T> {}
unsafe impl<T: Send> Sync for SpscQueue<T> {}

pub struct Producer<T> {
    queue: Arc<SpscQueue<T>>,
    chute: Arc<SpscQueue<T>>,
    default_cap: Option<usize>,
    eventfd: OwnedFd,
    b: [u8;8],
}

pub struct Consumer<T> {
    queue: Arc<SpscQueue<T>>,
    chute: Arc<SpscQueue<T>>,
    eventfd: OwnedFd,
    b: [u8;8],
}

impl<T> SpscQueue<T> {
    pub fn new(
        capacity: usize, 
        default_cap: Option<usize>
    ) -> io::Result<(Producer<T>, Consumer<T>)> {

        let queue = Arc::new(SpscQueue {
            buffer: (0..capacity).map(|_| UnsafeCell::new(None)).collect(),
            capacity,
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        });
        let eventfd: OwnedFd = EventFd::from_flags(
            EfdFlags::EFD_NONBLOCK | EfdFlags::EFD_CLOEXEC,
        )?.into();

        let chute_capacity = capacity / 3;
        let chute = Arc::new(SpscQueue {
            buffer: (0..chute_capacity).map(|_| UnsafeCell::new(None)).collect(),
            capacity: chute_capacity,
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        });

        Ok((
            Producer {
                queue: queue.clone(),
                chute: chute.clone(),
                default_cap,
                eventfd: unistd::dup(&eventfd)?,
                b: 1u64.to_ne_bytes()
            },
            Consumer { 
                eventfd, 
                queue, 
                chute,
                b: [0u8; 8],
            },
        ))
    }
}

impl<T: Msg> Producer<T> {
    pub fn try_push(&self, value: T) -> Result<(), T> {
        let res = push(&self.queue, value);
        if res.is_ok() {
            let _ = unistd::write(&self.eventfd, &self.b);
        }
        res
    }
    pub fn collect(&self) -> T { 
        match pop(&self.chute) {
            None => T::new(self.default_cap),
            Some(t) => t,
        }
    }
}

impl<T> Consumer<T> {
    pub fn pop(&self) -> Option<T> { pop(&self.queue) }
    pub fn recycle(&self, value: T) -> Result<(), T> { push(&self.chute, value) }
    pub fn read_event(&mut self) -> io::Result<usize> {
        self.b.fill(0);
        unistd::read(&self.eventfd, &mut self.b).map_err(|e|
            io::Error::from_raw_os_error(e as i32)
        )
    }
}

impl<T:Msg> Source for Consumer<T> {
    fn register(
            &mut self,
            registry: &mio::Registry,
            token: mio::Token,
            interests: mio::Interest,
        ) -> io::Result<()> {
        registry.register(&mut SourceFd(&self.eventfd.as_raw_fd()), token, interests)
        
    }
    fn reregister(
            &mut self,
            registry: &mio::Registry,
            token: mio::Token,
            interests: mio::Interest,
        ) -> io::Result<()> {
        registry.reregister(&mut SourceFd(&self.eventfd.as_raw_fd()), token, interests)
        
    }
    fn deregister(&mut self, registry: &mio::Registry) -> io::Result<()> {
        registry.deregister(&mut SourceFd(&self.eventfd.as_raw_fd()))
    }
}

fn pop<T>(queue: &Arc<SpscQueue<T>>) -> Option<T> {
    let head = queue.head.load(Ordering::Relaxed);
    let tail = queue.tail.load(Ordering::Acquire);

    if head == tail {
        return None;
    }

    let index = head % queue.capacity;
    let value = unsafe { (*queue.buffer[index].get()).take() };

    queue.head.store(head.wrapping_add(1), Ordering::Release);
    value
}

fn push<T>(queue: &Arc<SpscQueue<T>>, value: T) -> Result<(), T> {
    let tail = queue.tail.load(Ordering::Relaxed);
    let head = queue.head.load(Ordering::Acquire);

    if tail.wrapping_sub(head) == queue.capacity {
        return Err(value); // full
    }

    let index = tail % queue.capacity;
    unsafe { *queue.buffer[index].get() = Some(value); }

    queue.tail.store(tail.wrapping_add(1), Ordering::Release);
    Ok(())
}
