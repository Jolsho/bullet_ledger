use std::{io, os::fd::{AsRawFd,OwnedFd}};
use mio::{event::Source, unix::SourceFd};
use nix::{sys::eventfd::{EfdFlags, EventFd}, unistd};
use ringbuf::{traits::{Consumer, Producer, Split}, HeapCons, HeapProd, HeapRb};

pub struct MsgQ<T:Default> {
    pub q: HeapRb<Box<T>>,
    pub shute: HeapRb<Box<T>>,
}

impl<T:Default> MsgQ<T> {
    pub fn new(cap: usize) -> io::Result<Self> {
        Ok(Self { 
            q: HeapRb::new(cap), 
            shute: HeapRb::new(cap/2), 
        })
    }

    pub fn split(self) -> io::Result<(MsgProd<T>, MsgCons<T>)> {
        let (prod, cons) = self.q.split();
        let (recycler, collector) = self.shute.split();
        let event_fd: OwnedFd = EventFd::from_flags(EfdFlags::EFD_NONBLOCK | EfdFlags::EFD_CLOEXEC)?.into();
        let cons = MsgCons::new(cons, recycler, unistd::dup(&event_fd)?)?;
        let prod = MsgProd::new(prod, collector, event_fd)?;
        Ok((prod, cons))
    }
}

pub struct MsgProd<T> {
    producer: HeapProd<Box<T>>,
    collector: HeapCons<Box<T>>,
    eventfd: OwnedFd,
    b: [u8;8],
}

impl<T: Default> MsgProd<T> {
    pub fn new(
        producer: HeapProd<Box<T>>, 
        collector: HeapCons<Box<T>>,
        eventfd: OwnedFd,
    ) -> io::Result<Self> {
        Ok(Self { 
            producer, collector, eventfd, 
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
    b: [u8;8],
}

impl<T:Default> MsgCons<T> {
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

impl<T:Default> Source for MsgCons<T> {
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
