# The Process
The process of beginning to get into Objective C purely from `Rune`.

## Initial Aliases
The first step was understanding that Objective C objects were simply a `*var u8`:
```rune
type objc_object = *var u8
```

Therefore if we wanted to keep it as a Rune object we would need a mark to define what can be an object:
```rune
pub mark ObjC_Object {
    pub fn from_ptr(ptr: objc_object) -> Self
    pub fn as_ptr(&self) -> objc_object
}
```

## Modelling Objective C
Objective C has generally two types. An `id` which is a reference to an object and a `Selector` which is a reference to a selection (access) of an object.
```rune
/// An object reference — `id` in Objective-C.
pub struct id { object: objc_object }

/// A selector: an interned method name.
pub struct Selector { object: objc_object }
```

However, within an id there can be a class. A class is always an id but an id may **not always be** a class.

```rune
/// A class object. A class is an object too, which is why messages reach it
/// exactly the way they reach an instance.
pub struct Class { id: objc_object }
```

We then define and bind the above to `ObjC_Object`
```rune
bind ObjC_Object to objc_object {
    pub fn from_ptr(ptr: objc_object) -> Self { ptr }
    pub fn as_ptr(&self) -> objc_object { self }
}

bind ObjC_Object to id {
    pub fn from_ptr(ptr: objc_object) -> Self { id { object: ptr } }
    pub fn as_ptr(&self) -> objc_object { self.object }
}

bind ObjC_Object to Selector {
    pub fn from_ptr(ptr: objc_object) -> Self { Selector { object: ptr } }
    pub fn as_ptr(&self) -> objc_object { self.object }
}

bind ObjC_Object to Class {
    pub fn from_ptr(ptr: objc_object) -> Self { Class { id: ptr } }
    pub fn as_ptr(&self) -> objc_object { self.id }
}

// A C string is already the address of NUL-terminated bytes, which is exactly
// what `stringWithUTF8String:` is asking for.
bind ObjC_Object to CString {
    #safe("a CString is already the address of NUL-terminated bytes")
    pub fn from_ptr(ptr: objc_object) -> Self { unsafe { ptr as CString } }

    #safe("the same address, read back as the pointer it is")
    pub fn as_ptr(&self) -> objc_object { unsafe { self as *var u8 } }
}
```

## Exposing Objective C's API
All we really need for this barebones example is a couple functions:
```rune
extern "C" {
    pub fn objc_getClass(name: CString) -> objc_object
    pub fn sel_registerName(str: CString) -> objc_object
    pub fn objc_msgSend(id: objc_object, sel: objc_object, ...) -> objc_object
}
```

One particular head-ache inducing note to make is that objc_msgSend is not actually variadic, it is jump-based and calls the function 'selected' like you would for a normal function. This means that it reads registers (on arm64) `x0-x8`. However variadic functions are put onto the stack; causing a segfault or improper data passing. Luckily the resolution is simple... cast.
```rune
objcmsgSend as @cfunction(id: objc_object, sel: objc_object, string: CString) // for setting text/titles
```

However this is easily inefficient and a lot of repetetive typing allowing a more complex but readable solution to be using macros:
```rune
pub macro SendMsg {
    ($recv: expr, $sel: expr $(, $item: expr)*) => {
        (objc_msgSend as @cfunction(objc_object, objc_object
                                    $(, typeof($item))*) -> objc_object)(
            $recv.as_ptr(), $sel.as_ptr() $(, $item)*)
    }
}

// For when you simply want the function signature and the arguments may not match the types
pub macro SendMsgAs {
    ($($item: expr),*) => {
        objc_msgSend as @cfunction(objc_object, objc_object
                                   $(, $item)*) -> objc_object
    }
}
```

## Basic Geometry
Objective C requires some basic geometry structures for the purpose of this example which we'll implement below:
```rune
pub struct CGPoint { pub x: f64, pub y: f64 }
pub struct CGSize { pub width: f64, pub height: f64 }
pub struct CGRect { pub origin: CGPoint, pub size: CGSize }

// `(x, y)` and `(w, h)` are how a rectangle is written at a call site. These
// say what that means once, so no call site has to.
bind (f64, f64) into CGPoint {
    pub fn convert(&self) -> CGPoint { CGPoint { x: self.0, y: self.1 } }
}
bind (f64, f64) into CGSize {
    pub fn convert(&self) -> CGSize { CGSize { width: self.0, height: self.1 } }
}

extend CGRect {
    pub fn new(origin: (f64, f64), size: (f64, f64)) -> Self {
        CGRect { origin: origin, size: size }
    }
}
```

## Misc helpers
Finally some misc helpers before diving into the app loop:
```rune
enum Style {
    Titled = 1 << 0
    Closable = 1 << 1
    Resizable = 1 << 3
}

/// A `String` is what one writes; `stringWithUTF8String:` wants bytes.
#unsafe pub fn nsstring(text: String) -> id {
    Class::new("NSString").call("stringWithUTF8String:", text.$cstr())
}
```

## Application (main) Loop
We start with creating an app and window
```rune
    let app = Class::from_id(
        Class::new("NSApplication").select("sharedApplication"))

    var window = Class::from_id(Class::new("NSWindow").select("alloc"))
```

From there we begin initializing how our window should look:
```rune
    // The fields take `(f64, f64)` because `bind (f64, f64) into CGPoint`
    // has already said what that means.
    let rect = CGRect { origin: (100.0, 100.0), size: (500.0, 300.0) }
    let style: u64 = Titled as u64 | Closable as u64 | Resizable as u64

    window = Class::from_id(id::from_ptr(SendMsg!(
        window,
        Selector::new("initWithContentRect:styleMask:backing:defer:"),
        rect,
        style,
        2 as int,
        false
    )))

    window.call("setTitle:", nsstring("Hello World"))
```
Next, we build a hello world label in our window
```rune
    // A label, built and placed the same way.
    var text = Class::new("NSTextField").select("alloc")
    text = id::from_ptr(SendMsg!(text, Selector::new("initWithFrame:"),
                                 CGRect::new((170.0, 130.0), (160.0, 30.0))))
    text.call("setStringValue:", nsstring("Hello world!"))

    window.select("contentView").call("addSubview:", text)
```

Finally we bring the window to the front and call the run method from our app
```rune
    // `null_ptr()` is `nil`: the sender this message does not use. It goes through `dyn ObjC_Object` like any other argument.
    window.call("makeKeyAndOrderFront:", null_ptr())

    io::println("Hello from Objective-C!")
    app.select("run")
```