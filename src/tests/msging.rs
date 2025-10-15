

#[test]
fn msging() {
    use mio::{Interest, Poll, Token, Events};
    use crate::msging::MsgQ;
    use std::{thread::{self}, time::Duration};

    const U64:Token = Token(1);
    const STR:Token = Token(2);

    let q_num =  MsgQ::<u64>::new(12).unwrap();
    let q_string =  MsgQ::<String>::new(12).unwrap();

    let (mut num_prod, mut num_cons) = q_num.split().unwrap();
    let (mut string_prod, mut string_cons) = q_string.split().unwrap();

    let mut poll = Poll::new().unwrap();
    poll.registry().register(&mut num_cons, U64, Interest::READABLE).unwrap();
    poll.registry().register(&mut string_cons, STR, Interest::READABLE).unwrap();

    let num_sender = thread::spawn(move || {
        let mut num = Err(Box::new(0));
        for n in 0u64..10u64 {
            if n % 2 == 0 && num.is_ok() {
                num = Err(num_prod.collect());
            } else if num.is_ok() {
                num = Err(Box::new(n));
            }
            while num.is_err() {
                num = num_prod.push(num.unwrap_err());
                thread::yield_now();
            }
            thread::sleep(Duration::from_millis(100));
        }
    });

    let str_sender = thread::spawn(move || {
        let mut m = Err(Box::new(format!("THE STRING")));
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
                            println!("num(recycle @ n % 2):: {num}");
                            num_count += 1;
                            num_cons.recycle(num);
                        }
                    },
                    STR => {
                        let _ = string_cons.read_event();
                        while let Some(v) = string_cons.pop() {
                            println!("string(should recycle):: {v}");
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
