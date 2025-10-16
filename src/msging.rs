use std::{io, os::fd::{AsRawFd,OwnedFd}};
use mio::{event::Source, unix::SourceFd};
use nix::{sys::eventfd::{EfdFlags, EventFd}, unistd};
use ringbuf::{traits::{Consumer, Producer, Split}, HeapCons, HeapProd, HeapRb};

pub trait Msg {
    fn new(cap:Option<usize>) -> Self;
}

pub struct MsgQ<T:Msg> {
    pub q: HeapRb<Box<T>>,
    pub shute: HeapRb<Box<T>>,
    pub msg_buffer_cap: Option<usize>,
}

impl<T:Msg> MsgQ<T> {
    pub fn new(cap: usize, default_msg_buffer_cap: Option<usize>) -> io::Result<(MsgProd<T>, MsgCons<T>)> {
        let s = Self { 
            q: HeapRb::new(cap), 
            shute: HeapRb::new(cap/2), 
            msg_buffer_cap: default_msg_buffer_cap,
        };
        s.split()
    }

    fn split(self) -> io::Result<(MsgProd<T>, MsgCons<T>)> {
        let (prod, cons) = self.q.split();
        let (recycler, collector) = self.shute.split();
        let event_fd: OwnedFd = EventFd::from_flags(EfdFlags::EFD_NONBLOCK | EfdFlags::EFD_CLOEXEC)?.into();
        let cons = MsgCons::new(cons, recycler, unistd::dup(&event_fd)?)?;
        let prod = MsgProd::new(prod, collector, event_fd, self.msg_buffer_cap)?;
        Ok((prod, cons))
    }
}

pub struct MsgProd<T> {
    producer: HeapProd<Box<T>>,
    collector: HeapCons<Box<T>>,
    eventfd: OwnedFd,
    buffer_cap: Option<usize>,
    b: [u8;8],
}

impl<T: Msg> MsgProd<T> {
    pub fn new(
        producer: HeapProd<Box<T>>, 
        collector: HeapCons<Box<T>>,
        eventfd: OwnedFd,
        buffer_cap: Option<usize>
    ) -> io::Result<Self> {
        Ok(Self { 
            producer, collector, eventfd,  buffer_cap,
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
            None => Box::new(T::new(self.buffer_cap)),
        }
    }
}



pub struct MsgCons<T> {
    consumer: HeapCons<Box<T>>,
    recycler: HeapProd<Box<T>>,
    eventfd: OwnedFd,
    b: [u8;8],
}

impl<T:Msg> MsgCons<T> {
    pub fn new(
        consumer: HeapCons<Box<T>>, 
        recycler: HeapProd<Box<T>>, 
        eventfd: OwnedFd,
    ) -> io::Result<Self> {
        Ok(Self { 
            consumer, recycler, eventfd,
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

    pub fn read_event(&mut self) -> io::Result<usize> {
        self.b.fill(0);
        unistd::read(&self.eventfd, &mut self.b).map_err(|e|
            io::Error::from_raw_os_error(e as i32)
        )
    }
}

impl<T:Msg> Source for MsgCons<T> {
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
