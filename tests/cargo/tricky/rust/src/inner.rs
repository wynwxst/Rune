pub fn from_module() -> i32 {
    1
}

/// Exported from a submodule: the symbol is global all the same.
#[no_mangle]
pub extern "C" fn from_inner(a: f64, b: f32) -> f64 {
    a + b as f64
}
