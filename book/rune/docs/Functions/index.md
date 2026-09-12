# Functions
> A function is meant to solve a task that is repeated by providing the relevant code, callable as a unit

## Defining a function
```rune
fn myFunction(arg1: String, id: int) -> i64 {
// ...
}
```

The signature to define a function is effectively:
```
fn <function name>( [ <argument name> : <argument type> [ = <default value> ] ] ) -> [ <return type> ] {
    // body ...
}
```

## Purpose Of Functions
Say we want to replace all instances of `A` and `a` in a string with `Z`, we can achieve this with the below code snippet:
```rune
targetString: String = "A wizard shakes his wand"
var resultString: String = ""

for index in 0..targetString.$length() {
    character = targetString.$at(index)
    if character == 'A' || character == 'a' {
        resultString += 'Z'
    }
    else {
        resultString += character
    }
}

println!("{}",resultString)
```

What if we want to do this for more strings? Will we just copy the code over and over? This is where functions come in handy. Functions take parameter(s) from the programmer and produce a result. Let's make a function called `replaceAa` that takes a string and replaces `A` and `a` with a character provided by the programmer.

```rune
fn replaceAa(targetString: String, replaceWith: Character) -> String {
    var resultString: String = ""

    for index in 0..targetString.$length() {
        character = targetString.$at(index)
        if character == 'A' || character == 'a' {
            resultString += replaceWith // name the argument instead of 'Z'
        }
        else {
            resultString += character
        }
    }
    return resultString // return explicitly says, hand this value back to the programmer
}

// ...

// now we can do the below from anywhere
myResult = replaceAa("Meow spoke the awl", replaceWith: 'o')
println!("{}", myResult)
```

### Implicit Returns
While in the above example, we have return specifically, the last statement is **automatically** taken as a value to return so we can shorten that to:
```rune
fn replaceAa(targetString: String, replaceWith: Character) -> String {
    var resultString: String = ""
    if targetString.$length() == 0 {
        return "" // we can still explicitly declare a return anywhere
    }
    for index in 0..targetString.$length() {
        character = targetString.$at(index)
        if character == 'A' || character == 'a' {
            resultString += replaceWith
        }
        else {
            resultString += character
        }
    }
    resultString // we implicitly give back resultString
}
```

### Default Arguments
A function may also provide default values for it's arguments for example:
```rune
fn replaceAa(targetString: String, replaceWith: Character = 'Z') -> String {
    // ...
}
// ..
replaceAA("Hello Aib Aib") // replaceWith is by default Z
```

## Calling a function
The signature for calling a function is:
```
<function name>([ [ argumentName: ] <argument value> ])
```

For example all the below produce the same result:
```rune
// with no labels
var myResult = replaceAa("Meow spoke the awl", 'o')
// with a single label (replaceWith: )
myResult = replaceAa("Meow spoke the awl", replaceWith: 'o')
// with both labels
myResult = replaceAa(targetString: "Meow spoke the awl", replaceWith: 'o')
// not in order labels
myResult = replaceAa(replaceWith: 'o', targetString: "Meow spoke the awl")
```