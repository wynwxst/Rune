# Variables
> A variable can be thought of as a container holding a value. In simple terms it stores content and can be referred to
For example:
```rune
hello = "world"
my_variable = 1
your_variable = 3.5
ALL_UPPER = 'A'
Variable3 = "Bonjour"
camelCase = 10
snake_case = "Hsss"
```
For now ignore what variables are being assigned to but as you can see the way to declare a variable is:
```
<variable> = <value>
```

## Naming
Variables are flexible in how they may be named. Variables may contain `_`, Alphabet, Numbers (but cannot start with one).

## Types
If you are coming from a high-level language like Python you may be used to what is known as **dynamic typing** where you can assign a variable multiple types. Most low level languages require (the programmer to specify) or infer (the compiler guesses) the type of a variable.

You might wonder why this is done. In short, it is primarily done for speed, when you know the type of an object you can perform certain operations in advance.

```rune
// Like stated before, a type can be annotated or inferred

// annotated
// <variable> : <type> = <value>
x: int = 1 // the programmer tells the compiler, treat this as an integer (number)
y: String = "Hello World" // taken as a String
z: Character = 'M' // a single Character

// inferred
a = 1 // the compiler is smart enough to realise this is an integer
b = "Hello World" // recognised as a String
c = 'M' // recognised as a character
```
It is considered good practise to include the type annotation.

There are quite a few types in Rune, they will be covered later on.

## Mutability
Consider the below code:
```rune
x: int = 7 // declare x
x = 8 // update the value of x
```
The code will fail and will produce an error at `x = 8`. This is because of **Mutability**.

In simple terms Mutability refers to if a variable can be changed or not. **Immutable** means the value of a variable *cannot* be changed (__this is the default in Rune__). Whereas, **Mutable** means a value of a variable *can* be changed.

**By default all variables are Immutable in Rune**

However, you may use the `var` keyword to mark a variable as Mutable (Editable), so let's fix our above example.
```rune
var x: int = 7 // declare x AS MUTABLE
x = 8 // update the value of x
```

Although Immutable is the default, you can also explicitly declare it with the `let` keyword.

```rune
let immutable: String = "Hello "
var name: String = ""

name = "John"
// immutable = "Good Morning " (Error due to trying to change an immutable variable)
```

