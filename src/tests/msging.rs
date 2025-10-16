
use std::ops::{Deref, DerefMut};

use crate::msging::Msg;

#[derive(Debug)]
pub struct Num(u64);
impl Msg for Num {
    fn new(cap:Option<usize>) -> Self { 
        Num(cap.unwrap_or_default() as u64) 
    }
}
impl Deref for Num {
    type Target = u64;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

#[derive(Debug)]
pub struct Str(String);
impl Msg for Str {
    fn new(_cap:Option<usize>) -> Self { Str("TEST_STRING".to_string()) }
}
impl Deref for Str {
    type Target = String;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
impl DerefMut for Str {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

#[test]
fn msging() {
    use mio::{Interest, Poll, Token, Events};
    use crate::msging::MsgQ;
    use std::{thread::{self}, time::Duration};

    const U64:Token = Token(1);
    const STR:Token = Token(2);

    let (mut num_prod, mut num_cons) =  MsgQ::<Num>::new(12, None).unwrap();
    let (mut string_prod, mut string_cons) =  MsgQ::<Str>::new(12, None).unwrap();

    let mut poll = Poll::new().unwrap();
    poll.registry().register(&mut num_cons, U64, Interest::READABLE).unwrap();
    poll.registry().register(&mut string_cons, STR, Interest::READABLE).unwrap();

    let num_sender = thread::spawn(move || {
        let mut num = Err(Box::new(Num::new(None)));
        for n in 0u64..10u64 {
            if n % 2 == 0 && num.is_ok() {
                num = Err(num_prod.collect());
            } else if num.is_ok() {
                num = Err(Box::new(Num::new(Some(n as usize))));
            }
            while num.is_err() {
                num = num_prod.push(num.unwrap_err());
                thread::yield_now();
            }
            thread::sleep(Duration::from_millis(100));
        }
    });

    let str_sender = thread::spawn(move || {
        let mut m = Err(Box::new(Str::new(None)));
        for n in 0u8..10u8 {
            if m.is_ok() {
                let mut mm = string_prod.collect();
                mm.push(n as char);
                m = Err(mm);
            } 
            while m.is_err() {
                m = string_prod.push(m.unwrap_err());
                thread::yield_now();
            }
            thread::sleep(Duration::from_millis(100));
        }
    });

    let consumer = thread::spawn(move || {
        // Consumer thread
        let mut events = Events::with_capacity(16);
        let mut num_count = 0;
        let mut str_count = 0;
        loop {
            poll.poll(&mut events, Some(Duration::from_millis(100))).unwrap();
            for ev in events.iter() {
                match ev.token() {
                    U64 => {
                        let _ = num_cons.read_event();
                        while let Some(num) = num_cons.pop() {
                            println!("num(recycle @ n % 2):: {:?}", num);
                            num_count += 1;
                            num_cons.recycle(num);
                        }
                    },
                    STR => {
                        let _ = string_cons.read_event();
                        while let Some(v) = string_cons.pop() {
                            println!("string(should recycle):: {:?}", v);
                            str_count += 1;
                            string_cons.recycle(v);
                        }
                    },
                    _ => unreachable!(),
                }
                if num_count == 10 && str_count == 10 {
                    return;
                }
            }
        }
    });

    num_sender.join().unwrap();
    str_sender.join().unwrap();
    consumer.join().unwrap();
}
