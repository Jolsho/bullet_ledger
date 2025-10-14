use std::sync::atomic::{AtomicBool, Ordering};

pub static SHUTDOWN: AtomicBool = AtomicBool::new(false);

pub fn should_shutdown() -> bool {
    SHUTDOWN.load(Ordering::SeqCst)
}

#[allow(unused)]
pub fn request_shutdown() {
    SHUTDOWN.store(true, Ordering::SeqCst);
}
