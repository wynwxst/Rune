//! A small geometry library with a C ABI, for Rune to call.

use std::ffi::{c_char, CStr, CString};
use std::os::raw::c_int;

/// How many sides a regular polygon needs before it is drawn as a circle.
pub const CIRCLE_SIDES: u32 = 64;
/// The ratio of a circle's circumference to its diameter.
pub const PI: f64 = 3.141_592_653_589_793;
/// What this crate calls itself.
pub const NAME: &str = "geometry";

/// A point in the plane.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

/// An axis-aligned rectangle.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Rect {
    /// The corner nearest the origin.
    pub min: Point,
    pub max: Point,
}

/// Which way a turn goes.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Turn {
    Left = -1,
    Straight = 0,
    Right = 1,
}

/// The sum of two integers. Safe, and takes no addresses: Rune calls it
/// like one of its own functions.
#[no_mangle]
pub extern "C" fn add(a: i64, b: i64) -> i64 {
    a + b
}

/// The distance between two points.
#[no_mangle]
pub extern "C" fn distance(a: Point, b: Point) -> f64 {
    ((a.x - b.x).powi(2) + (a.y - b.y).powi(2)).sqrt()
}

/// The area of a rectangle.
#[no_mangle]
pub extern "C" fn rect_area(r: Rect) -> f64 {
    (r.max.x - r.min.x) * (r.max.y - r.min.y)
}

/// Which way the path a → b → c turns.
#[no_mangle]
pub extern "C" fn turn(a: Point, b: Point, c: Point) -> Turn {
    let cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if cross > 0.0 {
        Turn::Left
    } else if cross < 0.0 {
        Turn::Right
    } else {
        Turn::Straight
    }
}

/// The centroid of `count` points. Takes an address, so Rune calls it from
/// `unsafe`.
#[no_mangle]
pub extern "C" fn centroid(points: *const Point, count: usize) -> Point {
    let points = unsafe { std::slice::from_raw_parts(points, count) };
    let n = points.len().max(1) as f64;
    let (sx, sy) = points.iter().fold((0.0, 0.0), |(x, y), p| (x + p.x, y + p.y));
    Point { x: sx / n, y: sy / n }
}

/// Applies `f` to every integer in `0..count` and sums the results.
#[no_mangle]
pub extern "C" fn sum_mapped(count: c_int, f: extern "C" fn(c_int) -> c_int) -> c_int {
    (0..count).map(|i| f(i)).sum()
}

/// The length of a NUL-terminated string, in bytes.
///
/// # Safety
/// `text` must point to a NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn byte_length(text: *const c_char) -> usize {
    CStr::from_ptr(text).to_bytes().len()
}

/// A greeting, allocated by Rust. Free it with `free_text`.
#[no_mangle]
pub extern "C" fn greeting(times: u32) -> *mut c_char {
    let text = "hello from Rust! ".repeat(times as usize);
    CString::new(text.trim_end()).unwrap().into_raw()
}

/// Frees a string `greeting` made.
///
/// # Safety
/// `text` must have come from `greeting`, and is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn free_text(text: *mut c_char) {
    if !text.is_null() {
        drop(CString::from_raw(text));
    }
}

/// A running polygon, built a point at a time. Opaque to Rune.
pub struct Polygon {
    points: Vec<Point>,
}

/// A new, empty polygon.
#[no_mangle]
pub extern "C" fn polygon_new() -> *mut Polygon {
    Box::into_raw(Box::new(Polygon { points: Vec::new() }))
}

/// Adds a corner.
///
/// # Safety
/// `polygon` must have come from `polygon_new` and not have been freed.
#[no_mangle]
pub unsafe extern "C" fn polygon_push(polygon: *mut Polygon, p: Point) {
    (*polygon).points.push(p);
}

/// The polygon's area, by the shoelace formula.
///
/// # Safety
/// As for `polygon_push`.
#[no_mangle]
pub unsafe extern "C" fn polygon_area(polygon: *const Polygon) -> f64 {
    let pts = &(*polygon).points;
    let mut twice = 0.0;
    for i in 0..pts.len() {
        let (a, b) = (pts[i], pts[(i + 1) % pts.len()]);
        twice += a.x * b.y - b.x * a.y;
    }
    (twice / 2.0).abs()
}

/// Frees a polygon.
///
/// # Safety
/// `polygon` must have come from `polygon_new`, and is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn polygon_free(polygon: *mut Polygon) {
    if !polygon.is_null() {
        drop(Box::from_raw(polygon));
    }
}

/// Not bound: a Rust `String` has no C layout.
pub fn describe(p: Point) -> String {
    format!("({}, {})", p.x, p.y)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn adds() {
        assert_eq!(add(2, 3), 5);
    }
}

