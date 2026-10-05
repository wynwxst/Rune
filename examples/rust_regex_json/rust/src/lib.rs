//! A C ABI over `regex` and `serde_json`, for Rune.

use regex::Regex;
use serde_json::Value;
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;

/// Bumped when the functions below change.
pub const API_VERSION: u32 = 1;

/// A match: where it starts and ends, in bytes.
#[repr(C)]
pub struct Span {
    pub found: bool,
    pub start: usize,
    pub end: usize,
}

/// What a JSON value is.
#[repr(C)]
pub enum JsonKind {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
}

unsafe fn text<'a>(p: *const c_char) -> &'a str {
    CStr::from_ptr(p).to_str().unwrap_or("")
}

fn owned(s: String) -> *mut c_char {
    CString::new(s).map(CString::into_raw).unwrap_or(ptr::null_mut())
}

/// Frees any string this crate returned.
#[no_mangle]
pub unsafe extern "C" fn tk_free_text(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

/// The library's API version.
#[no_mangle]
pub extern "C" fn tk_version() -> u32 {
    API_VERSION
}

//=== regex ===

/// Compiles a pattern; null when it is not one. `rx_error` says why.
#[no_mangle]
pub unsafe extern "C" fn rx_new(pattern: *const c_char) -> *mut Regex {
    match Regex::new(text(pattern)) {
        Ok(r) => Box::into_raw(Box::new(r)),
        Err(_) => ptr::null_mut(),
    }
}

/// Why a pattern does not compile, or null when it does.
#[no_mangle]
pub unsafe extern "C" fn rx_error(pattern: *const c_char) -> *mut c_char {
    match Regex::new(text(pattern)) {
        Ok(_) => ptr::null_mut(),
        Err(e) => owned(e.to_string()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn rx_is_match(rx: *const Regex, haystack: *const c_char) -> bool {
    (*rx).is_match(text(haystack))
}

/// The first match at or after byte `from`.
#[no_mangle]
pub unsafe extern "C" fn rx_find(rx: *const Regex, haystack: *const c_char, from: usize) -> Span {
    let h = text(haystack);
    if from > h.len() {
        return Span { found: false, start: 0, end: 0 };
    }
    match (*rx).find_at(h, from) {
        Some(m) => Span { found: true, start: m.start(), end: m.end() },
        None => Span { found: false, start: 0, end: 0 },
    }
}

/// How many non-overlapping matches there are.
#[no_mangle]
pub unsafe extern "C" fn rx_count(rx: *const Regex, haystack: *const c_char) -> usize {
    (*rx).find_iter(text(haystack)).count()
}

/// Every match replaced; `$1` and `${name}` refer to groups.
#[no_mangle]
pub unsafe extern "C" fn rx_replace_all(
    rx: *const Regex,
    haystack: *const c_char,
    replacement: *const c_char,
) -> *mut c_char {
    owned((*rx).replace_all(text(haystack), text(replacement)).into_owned())
}

/// Calls `each(user, index, group)` for every capture group of the first
/// match; returns how many groups there were, 0 when nothing matched.
#[no_mangle]
pub unsafe extern "C" fn rx_captures(
    rx: *const Regex,
    haystack: *const c_char,
    each: extern "C" fn(*mut c_void, usize, *const c_char),
    user: *mut c_void,
) -> usize {
    let Some(caps) = (*rx).captures(text(haystack)) else { return 0 };
    for (i, group) in caps.iter().enumerate() {
        let s = CString::new(group.map(|m| m.as_str()).unwrap_or("")).unwrap();
        each(user, i, s.as_ptr());
    }
    caps.len()
}

#[no_mangle]
pub unsafe extern "C" fn rx_free(rx: *mut Regex) {
    if !rx.is_null() {
        drop(Box::from_raw(rx));
    }
}

//=== serde_json ===

/// Parses JSON; null when it is not. `json_error` says why.
#[no_mangle]
pub unsafe extern "C" fn json_parse(source: *const c_char) -> *mut Value {
    match serde_json::from_str::<Value>(text(source)) {
        Ok(v) => Box::into_raw(Box::new(v)),
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn json_error(source: *const c_char) -> *mut c_char {
    match serde_json::from_str::<Value>(text(source)) {
        Ok(_) => ptr::null_mut(),
        Err(e) => owned(e.to_string()),
    }
}

/// The value at a JSON pointer (`/items/0/name`), borrowed from `root`;
/// null when there is none.
#[no_mangle]
pub unsafe extern "C" fn json_pointer(root: *const Value, path: *const c_char) -> *const Value {
    match (*root).pointer(text(path)) {
        Some(v) => v as *const Value,
        None => ptr::null(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn json_kind(v: *const Value) -> JsonKind {
    match &*v {
        Value::Null => JsonKind::Null,
        Value::Bool(_) => JsonKind::Bool,
        Value::Number(_) => JsonKind::Number,
        Value::String(_) => JsonKind::String,
        Value::Array(_) => JsonKind::Array,
        Value::Object(_) => JsonKind::Object,
    }
}

/// The number, written to `out`; false when the value is not one.
#[no_mangle]
pub unsafe extern "C" fn json_number(v: *const Value, out: *mut f64) -> bool {
    match (*v).as_f64() {
        Some(n) => {
            *out = n;
            true
        }
        None => false,
    }
}

/// A string value, copied; null when the value is not a string.
#[no_mangle]
pub unsafe extern "C" fn json_string(v: *const Value) -> *mut c_char {
    match (*v).as_str() {
        Some(s) => owned(s.to_string()),
        None => ptr::null_mut(),
    }
}

/// The length of an array or object; 0 for anything else.
#[no_mangle]
pub unsafe extern "C" fn json_len(v: *const Value) -> usize {
    match &*v {
        Value::Array(a) => a.len(),
        Value::Object(o) => o.len(),
        _ => 0,
    }
}

/// Sets `key` on an object to a number.
#[no_mangle]
pub unsafe extern "C" fn json_set_number(v: *mut Value, key: *const c_char, n: f64) -> bool {
    match (*v).as_object_mut() {
        Some(o) => {
            o.insert(text(key).to_string(), serde_json::json!(n));
            true
        }
        None => false,
    }
}

/// The value as JSON text, pretty or compact.
#[no_mangle]
pub unsafe extern "C" fn json_to_string(v: *const Value, pretty: bool) -> *mut c_char {
    let s = if pretty {
        serde_json::to_string_pretty(&*v)
    } else {
        serde_json::to_string(&*v)
    };
    owned(s.unwrap_or_default())
}

#[no_mangle]
pub unsafe extern "C" fn json_free(v: *mut Value) {
    if !v.is_null() {
        drop(Box::from_raw(v));
    }
}
