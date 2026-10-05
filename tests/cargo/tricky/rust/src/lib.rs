//! Everything awkward a crate's C ABI can contain, for the binding reader.
#![allow(clippy::missing_safety_doc)]

use std::ffi::c_void;
use std::os::raw::{c_char, c_int, c_long};

mod inner;

/// Strings and lifetimes that look like items must not be read as items.
pub const TRICK: &str = "fn fake() {} #[no_mangle] pub extern \"C\" fn nope()";
pub const RAW: &str = r#"pub extern "C" fn also_nope() {}"#;
pub const BIG: u64 = 0xFFFF_FFFF;
pub const NEG: i32 = -42;
pub const BITS: u8 = 0b1010_1010;
pub const RATIO: f32 = 2.5;
pub const ON: bool = true;
const PRIVATE: i32 = 7;

#[repr(u8)]
pub enum Level {
    /// The lowest.
    Low = 1,
    Mid,
    High = 10,
}

#[repr(C)]
pub enum Shaped {
    Circle(f64),
    Square(f64),
}

#[repr(C)]
pub union Bits {
    pub i: u32,
    pub f: f32,
}

#[repr(C)]
pub struct Pair(pub i32, pub i32);

#[repr(C)]
pub struct Generic<T> {
    pub value: T,
}

#[repr(C)]
pub struct Opaque {
    _private: [u8; 0],
}

#[repr(C)]
pub struct Header {
    /// A tag.
    pub tag: [u8; 4],
    pub count: c_long,
    pub level: Level,
}

#[repr(C)]
pub struct Packet {
    pub header: Header,
    pub payload: *const u8,
    pub on_done: Option<extern "C" fn(*mut c_void, c_int)>,
}

#[repr(C)]
pub struct HasString {
    pub name: String,
}

#[no_mangle]
pub extern "C" fn level_of(n: i32) -> Level {
    if n > 5 { Level::High } else if n > 0 { Level::Mid } else { Level::Low }
}

#[unsafe(no_mangle)]
pub extern "C" fn edition_2024(type_: u32, r#in: u32) -> u32 {
    type_ + r#in
}

#[export_name = "renamed_symbol"]
pub extern "C" fn rust_side_name(x: u64) -> u64 {
    x * 3
}

#[no_mangle]
pub extern "C" fn header_count(h: &Header) -> c_long {
    h.count
}

#[no_mangle]
pub extern "C" fn maybe(h: Option<&mut Header>) -> bool {
    h.is_some()
}

#[no_mangle]
pub extern "C" fn opaque_new() -> *mut Opaque {
    std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn packet_size(p: Packet) -> usize {
    p.header.tag.len()
}

#[no_mangle]
pub extern "C" fn takes_string(_s: String) {}

#[no_mangle]
pub extern "C" fn takes_slice(_s: &[u8]) {}

#[no_mangle]
pub extern "C" fn takes_rust_fn(_f: fn(i32)) {}

#[no_mangle]
pub extern "C" fn generic_one<T>(_t: T) {}

#[no_mangle]
pub extern "C" fn never_returns() -> ! {
    std::process::abort()
}

#[no_mangle]
pub extern "C" fn label(_c: *const c_char) -> c_int { 0 }

pub struct Thing;

/// A character literal that is not ASCII: once read as a lifetime, which
/// lost the reader its place for the rest of the file.
pub fn greek_first(s: &str) -> bool {
    s.starts_with('α') || s.ends_with('Ω')
}

#[repr(transparent)]
pub struct Wrapped {
    pub inner: u32,
}

mod hidden {
    /// Inside a private module: not the crate's API.
    pub const HIDDEN: u32 = 9;

    #[repr(C)]
    pub struct Twice {
        pub a: i32,
    }
}

mod other {
    #[repr(C)]
    pub struct Twice {
        pub b: i64,
    }
}

impl Thing {
    /// An associated constant: not a global.
    pub const ASSOCIATED: u32 = 3;

    #[no_mangle]
    pub extern "C" fn thing_static(x: i32) -> i32 { x + 1 }
}

extern "C" {
    fn abs(x: c_int) -> c_int;
}

#[no_mangle]
pub extern "C" fn uses_abs(x: c_int) -> c_int {
    unsafe { abs(x) }
}

fn private_helper() -> i32 {
    PRIVATE
}

#[no_mangle]
pub extern "C" fn helper() -> i32 {
    private_helper() + inner::from_module()
}

#[cfg(test)]
mod tests {
    #[no_mangle]
    pub extern "C" fn test_only() {}
}
