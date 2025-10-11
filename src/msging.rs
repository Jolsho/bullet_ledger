use std::{io, ops::Deref, os::fd::{AsRawFd, FromRawFd, OwnedFd}};
use nix::{libc,sys::{epoll::{Epoll, EpollCreateFlags, EpollEvent, EpollFlags}, eventfd::EventFd}, unistd};
use ringbuf::{traits::{Consumer, Producer, Split}, HeapCons, HeapProd, HeapRb};

pub struct Poller(Epoll);
impl Poller {
    pub fn new() -> nix::Result<Self> {
        Ok(Poller(Epoll::new(EpollCreateFlags::empty())?))
    }
    pub fn listen_to<T: Pollable>(&mut self, q: &T) -> nix::Result<()> {
        self.add(q.get_eventfd(), q.clone_event())
    }
}
impl Deref for Poller {
    type Target = Epoll;
    fn deref(&self) -> &Self::Target { &self.0 }
}
pub trait Pollable {
    fn get_eventfd(&self) -> &OwnedFd;
    fn clone_event(&self) -> EpollEvent;
}

pub type Ring<T> = HeapRb<Box<T>>;
pub type RingProd<T> = HeapProd<Box<T>>;
pub type RingCons<T> = HeapCons<Box<T>>;

pub struct MsgQ<T:Default> {
    pub q: HeapRb<Box<T>>,
    pub shute: HeapRb<Box<T>>,
    pub eventfd: OwnedFd,
    pub event: EpollEvent,
}

impl<T:Default> MsgQ<T> {
    pub fn new(cap: usize, event_data: i32) -> nix::Result<Self> {
        Ok(Self { 
            q: HeapRb::new(cap), 
            shute: HeapRb::new(cap/2), 
            eventfd: EventFd::new()?.into(), 
            event: EpollEvent::new(EpollFlags::EPOLLIN, event_data as u64),
        })
    }

    pub fn split(self) -> io::Result<(MsgProd<T>, MsgCons<T>)> {
        let (prod, cons) = self.q.split();
        let (recycler, collector) = self.shute.split();
        let prod = MsgProd::new(prod, collector, &self.eventfd, &self.event)?;
        let cons = MsgCons::new(cons, recycler, &self.eventfd, &self.event)?;
        Ok((prod, cons))
    }
}
impl<T:Default> Pollable for MsgQ<T> {
    fn get_eventfd(&self) -> &OwnedFd { &self.eventfd }
    fn clone_event(&self) -> EpollEvent { self.event.clone() }
}



pub struct MsgProd<T> {
    producer: HeapProd<Box<T>>,
    collector: HeapCons<Box<T>>,
    eventfd: OwnedFd,
    event: EpollEvent,
    b: [u8;8],
}

impl<T: Default> MsgProd<T> {
    pub fn new(
        producer: HeapProd<Box<T>>, 
        collector: HeapCons<Box<T>>,
        fd: &OwnedFd, event: &EpollEvent,
    ) -> io::Result<Self> {
        Ok(Self { 
            producer, collector,
            eventfd: clone_owned_fd(fd)?, 
            event: event.clone(), 
            b: 1u64.to_ne_bytes()
        })
    }

    pub fn push(&mut self, t: Box<T>) -> Result<(), Box<T>> {
        let res = self.producer.try_push(t);
        if res.is_ok() {
            let _ = unistd::write(&self.eventfd, &self.b);
        }
        res
    }

    pub fn collect(&mut self) -> Box<T> {
        match self.collector.try_pop() {
            Some(m) => m,
            None => Box::new(T::default()),
        }
    }
}



pub struct MsgCons<T> {
    consumer: HeapCons<Box<T>>,
    recycler: HeapProd<Box<T>>,
    eventfd: OwnedFd,
    event: EpollEvent,
    b: [u8;8],
}

impl<T:Default> MsgCons<T> {
    pub fn new(
        consumer: HeapCons<Box<T>>, 
        recycler: HeapProd<Box<T>>, 
        fd: &OwnedFd, event: &EpollEvent,
    ) -> io::Result<Self> {
        Ok(Self { 
            consumer, recycler,
            eventfd: clone_owned_fd(fd)?, 
            event: event.clone(), 
            b: [0u8;8] 
        })
    }

    pub fn pop(&mut self) -> Option<Box<T>> {
        self.consumer.try_pop()
    }

    pub fn recycle(&mut self, t: Box<T>) {
        if let Err(o) =  self.recycler.try_push(t) {
            drop(o);
        }
    }

    pub fn read_event(&mut self) -> nix::Result<usize> {
        self.b.fill(0);
        unistd::read(&self.eventfd, &mut self.b)
    }
}

impl<T> Pollable for MsgCons<T> {
    fn get_eventfd(&self) -> &OwnedFd { &self.eventfd }
    fn clone_event(&self) -> EpollEvent { self.event.clone() }
}


pub fn clone_owned_fd(fd: &OwnedFd) -> io::Result<OwnedFd> {
    let new_fd = unsafe { libc::dup(fd.as_raw_fd()) };
    if new_fd < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(unsafe { OwnedFd::from_raw_fd(new_fd) })
    }
}
