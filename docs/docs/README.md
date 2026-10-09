# Rune language reference

A low-level, statically typed language with automatic reference counting and optional memory safety. This is the complete reference — every construct, every variant, with examples compiled by `runec` as this page was built.

Version `v0.1.0`. One folder per category, one file per heading.

## Getting started

Rune is a statically typed, compiled language with automatic reference counting and safety you can dial down. It reads like Swift and Rust, compiles through LLVM, and has no runtime beyond a small C library.

- [Building the toolchain](Getting_Started/building_the_toolchain.md)
- [Your first program](Getting_Started/your_first_program.md)
- [Using the package manager](Getting_Started/using_the_package_manager.md)
- [How to read this reference](Getting_Started/how_to_read_this_reference.md)

## Lexical structure

How source text becomes tokens: where statements end, what a literal can look like, and which words are reserved.

- [Statements and line breaks](Lexical_Structure/statements_and_line_breaks.md)
- [Comments](Lexical_Structure/comments.md)
- [Integer literals](Lexical_Structure/integer_literals.md)
- [Float literals](Lexical_Structure/float_literals.md)
- [Characters and escapes](Lexical_Structure/characters_and_escapes.md)
- [String literals](Lexical_Structure/string_literals.md)
- [`.` and `::`](Lexical_Structure/dot_and_coloncolon.md)
- [Reserved words](Lexical_Structure/reserved_words.md)

## Bindings and scope

Names are immutable unless you say otherwise. Blocks are expressions, so a binding can be initialised by a whole computation.

- [Declaring a binding](Bindings_And_Scope/declaring_a_binding.md)
- [Immutability is checked](Bindings_And_Scope/immutability_is_checked.md)
- [Destructuring](Bindings_And_Scope/destructuring.md)
- [Scope and shadowing](Bindings_And_Scope/scope_and_shadowing.md)
- [Globals](Bindings_And_Scope/globals.md)
- [Blocks are expressions](Bindings_And_Scope/blocks_are_expressions.md)

## Types

Rune has no implicit narrowing and no truthiness. Every type below is written the same way in a declaration, a parameter list and a cast.

- [The full set](Types/the_full_set.md)
- [Function types](Types/function_types.md)
- [Type aliases](Types/type_aliases.md)
- [`typeof`: the type an expression has](Types/typeof_the_type_an_expression_has.md)
- [Conversions that happen on their own](Types/conversions_that_happen_on_their_own.md)
- [`into`: conversions you write yourself](Types/into_conversions_you_write_yourself.md)
- [Where a conversion you wrote happens on its own](Types/where_a_conversion_you_wrote_happens_on_its_own.md)
- [Conversions you have to ask for](Types/conversions_you_have_to_ask_for.md)
- [There is no truthiness](Types/there_is_no_truthiness.md)

## Operators

The full set, in precedence order, with the mark method each one dispatches to when its operands are not builtin.

- [Precedence](Operators/precedence.md)
- [Arithmetic](Operators/arithmetic.md)
- [Bitwise and shifts](Operators/bitwise_and_shifts.md)
- [Comparison](Operators/comparison.md)
- [Logical operators](Operators/logical_operators.md)
- [Nil coalescing](Operators/nil_coalescing.md)
- [Assignment](Operators/assignment.md)
- [Ranges](Operators/ranges.md)
- [`as` and `is`](Operators/as_and_is.md)

## Conditionals

`if` is an expression, so it can produce a value. Every branch has to agree on the type of that value — when anything is going to use it.

- [`if` as a statement](Conditionals/if_as_a_statement.md)
- [`if` as an expression](Conditionals/if_as_an_expression.md)
- [Testing and binding at once](Conditionals/testing_and_binding_at_once.md)
- [When the arms need not agree](Conditionals/when_the_arms_need_not_agree.md)
- [An `if` with no `else` produces nothing](Conditionals/an_if_with_no_else_produces_nothing.md)

## Loops

Three loop forms. `loop` can carry a value out through `break`; `while` and `for` always evaluate to `()`.

- [`while`](Loops/while.md)
- [`loop` and `break` with a value](Loops/loop_and_break_with_a_value.md)
- [`for`](Loops/for.md)
- [`break` and `continue`](Loops/break_and_continue.md)
- [Labelled loops](Loops/labelled_loops.md)
- [`defer`](Loops/defer.md)

## Iterators

`for` walks three shapes natively. Everything else it asks, through two marks that are in scope everywhere — because `for` is syntax, and what syntax dispatches through cannot need an import.

- [The two marks](Iterators/the_two_marks.md)
- [Writing one](Iterators/writing_one.md)
- [Writing a sequence](Iterators/writing_a_sequence.md)
- [What the loop advances](Iterators/what_the_loop_advances.md)
- [Destructuring](Iterators/destructuring.md)
- [Reshaping one](Iterators/reshaping_one.md)
- [Ending a chain](Iterators/ending_a_chain.md)
- [Stopping early](Iterators/stopping_early.md)
- [Numbering](Iterators/numbering.md)
- [Returning a type parameterised by `Self`](Iterators/returning_a_type_parameterised_by_self.md)
- [From the library](Iterators/from_the_library.md)
- [Iterating generically](Iterators/iterating_generically.md)

## Pattern matching

`match` is exhaustive: the compiler lists the cases you have not handled. Patterns work in `match`, in `if ... is`, in `for`, and on the left of a binding.

- [Every kind of pattern](Pattern_Matching/every_kind_of_pattern.md)
- [Slice and array patterns](Pattern_Matching/slice_and_array_patterns.md)
- [Bindings borrow; `take` moves](Pattern_Matching/bindings_borrow_take_moves.md)
- [Bindings in alternatives](Pattern_Matching/bindings_in_alternatives.md)
- [Exhaustiveness](Pattern_Matching/exhaustiveness.md)
- [Matching through a borrow](Pattern_Matching/matching_through_a_borrow.md)
- [`match` produces a value](Pattern_Matching/match_produces_a_value.md)

## Functions

Parameters may have defaults and be passed by label. The last expression is the result. Functions nest, and a function is a value.

- [Declaring a function](Functions/declaring_a_function.md)
- [`Never`: a function that does not return](Functions/never_a_function_that_does_not_return.md)
- [Default arguments](Functions/default_arguments.md)
- [Labelled arguments](Functions/labelled_arguments.md)
- [Recursion and nesting](Functions/recursion_and_nesting.md)
- [Mutable parameters](Functions/mutable_parameters.md)

## Closures and function values

A closure is written `||(params) -> Result { ... }`. It captures by value, a named function converts to a function value on its own, and the types may be left out wherever the context already says them.

- [Writing a closure](Closures_And_Function_Values/writing_a_closure.md)
- [Leaving the types out](Closures_And_Function_Values/leaving_the_types_out.md)
- [Captures](Closures_And_Function_Values/captures.md)
- [Passing functions around](Closures_And_Function_Values/passing_functions_around.md)
- [Closures over reference types](Closures_And_Function_Values/closures_over_reference_types.md)

## Structs

A struct is a value type: assigning one copies it. Fields are private unless marked `pub`.

- [Declaring and building](Structs/declaring_and_building.md)
- [Update syntax](Structs/update_syntax.md)
- [Methods](Structs/methods.md)
- [Structs are copied](Structs/structs_are_copied.md)
- [Privacy](Structs/privacy.md)
- [Destroying a value](Structs/destroying_a_value.md)
- [Handing an owning value on](Structs/handing_an_owning_value_on.md)
- [`#resource`: a field that has to be released](Structs/resource_a_field_that_has_to_be_released.md)
- [One struct extending another](Structs/one_struct_extending_another.md)

## Enums

An enum is a value that is exactly one of several shapes. Variants may carry nothing, a tuple, or named fields.

- [The three variant shapes](Enums/the_three_variant_shapes.md)
- [Explicit discriminants](Enums/explicit_discriminants.md)
- [Methods on an enum](Enums/methods_on_an_enum.md)
- [Generic enums](Enums/generic_enums.md)
- [Recursive shapes need a class](Enums/recursive_shapes_need_a_class.md)
- [Variant names are a convenience, not a declaration](Enums/variant_names_are_a_convenience_not_a_declaration.md)
- [A leading dot: whatever type is wanted here](Enums/a_leading_dot_whatever_type_is_wanted_here.md)
- [One enum extending another](Enums/one_enum_extending_another.md)

## Classes

A class is a reference type: assigning one shares the instance. Classes have single inheritance, virtual methods, and destructors that run deterministically.

- [Declaring a class](Classes/declaring_a_class.md)
- [Classes are shared, not copied](Classes/classes_are_shared_not_copied.md)
- [Inheritance and `super`](Classes/inheritance_and_super.md)
- [Destruction order](Classes/destruction_order.md)
- [Asking what a value really is](Classes/asking_what_a_value_really_is.md)
- [Static methods](Classes/static_methods.md)

## Marks

A mark is Rune's trait: a set of methods a type can promise to provide. Marks bind to any type, including the builtin ones.

- [Declaring a mark](Marks/declaring_a_mark.md)
- [Unimplemented requirements are caught](Marks/unimplemented_requirements_are_caught.md)
- [Super-marks](Marks/super_marks.md)
- [Binding to builtin types](Marks/binding_to_builtin_types.md)
- [Conditional bindings](Marks/conditional_bindings.md)
- [`Self`: requirements that build](Marks/self_requirements_that_build.md)
- [When a type answers for itself](Marks/when_a_type_answers_for_itself.md)
- [One name, several parameter lists](Marks/one_name_several_parameter_lists.md)
- [Associated types](Marks/associated_types.md)
- [`extend`: methods without a mark](Marks/extend_methods_without_a_mark.md)
- [Mark objects: `dyn Mark`](Marks/mark_objects_dyn_mark.md)
- [Opaque results: `some Mark`](Marks/opaque_results_some_mark.md)
- [A requirement with a `where` clause](Marks/a_requirement_with_a_where_clause.md)

## `Any`

`Any` holds one value of any type and remembers which. Every question about it is answered from the value itself, so the answer cannot be wrong.

- [Putting a value in](Any/putting_a_value_in.md)
- [Getting it back out](Any/getting_it_back_out.md)
- [What the answer is based on](Any/what_the_answer_is_based_on.md)
- [Ownership](Any/ownership.md)
- [When it aborts](Any/when_it_aborts.md)
- [Printing one](Any/printing_one.md)
- [Which of the three to reach for](Any/which_of_the_three_to_reach_for.md)

## Compile-time reflection

`std::reflect` asks the compiler what it already knows in order to lay a value out. Every answer is settled while compiling, so a call costs what its answer costs — usually nothing at all.

- [Asking about a type](Compile_Time_Reflection/asking_about_a_type.md)
- [It really is compile time](Compile_Time_Reflection/it_really_is_compile_time.md)
- [Reading a value](Compile_Time_Reflection/reading_a_value.md)
- [What is not here](Compile_Time_Reflection/what_is_not_here.md)

## Operator overloading

`bind operator::name to Type` gives an operator a meaning for your type. Every operator maps to one method name.

- [The mapping](Operator_Overloading/the_mapping.md)
- [Arithmetic](Operator_Overloading/arithmetic.md)
- [Equality and ordering](Operator_Overloading/equality_and_ordering.md)
- [Subscripting](Operator_Overloading/subscripting.md)
- [Dereference: making a value behave like a pointer](Operator_Overloading/dereference_making_a_value_behave_like_a_pointer.md)
- [One operator, several right-hand types](Operator_Overloading/one_operator_several_right_hand_types.md)
- [Extending a builtin, without overriding it](Operator_Overloading/extending_a_builtin_without_overriding_it.md)
- [What cannot be overloaded](Operator_Overloading/what_cannot_be_overloaded.md)
- [Automatic marks](Operator_Overloading/automatic_marks.md)

## Generics

Type parameters are monomorphised: each set of arguments produces its own specialised copy, so there is no boxing and no dynamic dispatch.

- [Generic functions](Generics/generic_functions.md)
- [Generic methods](Generics/generic_methods.md)
- [Bounds](Generics/bounds.md)
- [Generic types](Generics/generic_types.md)
- [Generic methods](Generics/generic_methods_2.md)
- [How instantiation is reported](Generics/how_instantiation_is_reported.md)
- [Specialisation](Generics/specialisation.md)
- [Binding a shape](Generics/binding_a_shape.md)

## Option and Result

Both are ordinary Rune enums that the compiler knows by name. `T?`, `nil`, `??` and `?` are sugar over them, and their variants are in scope everywhere.

- [What they are](Option_And_Result/what_they_are.md)
- [Producing an Option](Option_And_Result/producing_an_option.md)
- [Consuming an Option](Option_And_Result/consuming_an_option.md)
- [Result](Option_And_Result/result.md)
- [`?`, on both](Option_And_Result/question_on_both.md)
- [`?` converts the error](Option_And_Result/question_converts_the_error.md)
- [Options of reference types](Option_And_Result/options_of_reference_types.md)

## Arrays, slices and tuples

An array has a fixed, constant length. A slice borrows part of one. A tuple groups a fixed set of types.

- [Arrays](Arrays_Slices_And_Tuples/arrays.md)
- [Bounds checking](Arrays_Slices_And_Tuples/bounds_checking.md)
- [Slices](Arrays_Slices_And_Tuples/slices.md)
- [Tuples](Arrays_Slices_And_Tuples/tuples.md)
- [Arrays of aggregates](Arrays_Slices_And_Tuples/arrays_of_aggregates.md)

## Strings and characters

`String` is owned, reference counted and UTF-8. `Character` is one Unicode scalar. `CString` is a borrowed pointer for talking to C.

- [The three text types](Strings_And_Characters/the_three_text_types.md)
- [Building strings](Strings_And_Characters/building_strings.md)
- [Inspecting a string](Strings_And_Characters/inspecting_a_string.md)
- [Characters: `text[i]` and `for c in text`](Strings_And_Characters/characters_text_i_and_for_c_in_text.md)
- [Parsing](Strings_And_Characters/parsing.md)
- [Characters](Strings_And_Characters/characters.md)
- [Normalisation and collation](Strings_And_Characters/normalisation_and_collation.md)
- [Splitting, trimming and replacing](Strings_And_Characters/splitting_trimming_and_replacing.md)
- [Comparison and sorting](Strings_And_Characters/comparison_and_sorting.md)

## Formatting and printing

`println!` writes a formatted line, `print!` writes without one, and `format!` builds the `String` both of them print. The format string is read at compile time, so what it asks for is checked then.

- [Placeholders](Formatting_And_Printing/placeholders.md)
- [Format options](Formatting_And_Printing/format_options.md)
- [What can be formatted](Formatting_And_Printing/what_can_be_formatted.md)
- [When arguments are evaluated](Formatting_And_Printing/when_arguments_are_evaluated.md)
- [The pieces underneath](Formatting_And_Printing/the_pieces_underneath.md)

## Memory and reference counting

Class instances are reference counted. The compiler inserts every retain and release itself, at the points a careful C programmer would have written them, and a debug build tells you if any object outlived the program.

- [What is counted, and what is not](Memory_And_Reference_Counting/what_is_counted_and_what_is_not.md)
- [Sharing an object](Memory_And_Reference_Counting/sharing_an_object.md)
- [Deinitialisers](Memory_And_Reference_Counting/deinitialisers.md)
- [Values have destructors too](Memory_And_Reference_Counting/values_have_destructors_too.md)
- [Counting that is not emitted](Memory_And_Reference_Counting/counting_that_is_not_emitted.md)
- [Cycles are refused, not collected](Memory_And_Reference_Counting/cycles_are_refused_not_collected.md)
- [`Unique`: one owner, so no ring](Memory_And_Reference_Counting/unique_one_owner_so_no_ring.md)
- [`weak`, in detail](Memory_And_Reference_Counting/weak_in_detail.md)
- [Leak reporting](Memory_And_Reference_Counting/leak_reporting.md)
- [Where the counting happens](Memory_And_Reference_Counting/where_the_counting_happens.md)
- [Three owning pointers, and what tells them apart](Memory_And_Reference_Counting/three_owning_pointers_and_what_tells_them_apart.md)
- [Reaching through a stand-in](Memory_And_Reference_Counting/reaching_through_a_stand_in.md)

## Pointers, borrows and slices

Four kinds of indirection, in order of how much the compiler will do for you: shared borrows, mutable borrows, slices, and raw pointers.

- [Shared and mutable borrows](Pointers_Borrows_And_Slices/shared_and_mutable_borrows.md)
- [Slices](Pointers_Borrows_And_Slices/slices.md)
- [Raw pointers](Pointers_Borrows_And_Slices/raw_pointers.md)

## Safety levels

Rune is memory safe by default and lets you turn that off deliberately, per build or per function. Nothing is unchecked by accident.

- [The three levels](Safety_Levels/the_three_levels.md)
- [Integer overflow](Safety_Levels/integer_overflow.md)
- [`#unsafe` and `unsafe { }`](Safety_Levels/unsafe_and_unsafe_block.md)
- [`#safe("reason")`](Safety_Levels/safe_reason.md)
- [Borrows and ownership](Safety_Levels/borrows_and_ownership.md)
- [What it buys at run time](Safety_Levels/what_it_buys_at_run_time.md)
- [What is still your responsibility](Safety_Levels/what_is_still_your_responsibility.md)

## Single ownership without a count

Reference counting is the default, but it is not the only choice. Built with `--memory zombie`, a program keeps no counts at all: every value has exactly one owner, is handed on by moving, and is destroyed the moment its owner's scope ends. A second, precise borrow checker — Zombie — proves that every borrow is finished before the value it points at is gone, so nothing dangles and nothing is freed twice.

- [Turning it on](Single_Ownership_Without_A_Count/turning_it_on.md)
- [Values move; borrows look](Single_Ownership_Without_A_Count/values_move_borrows_look.md)
- [The checker is precise](Single_Ownership_Without_A_Count/the_checker_is_precise.md)
- [Reference-counted code, ported](Single_Ownership_Without_A_Count/reference_counted_code_ported.md)
- [Where a reference is borrowed from](Single_Ownership_Without_A_Count/where_a_reference_is_borrowed_from.md)
- [Views: which fields a method touches](Single_Ownership_Without_A_Count/views_which_fields_a_method_touches.md)
- [Internal references](Single_Ownership_Without_A_Count/internal_references.md)
- [The standard library runs on it](Single_Ownership_Without_A_Count/the_standard_library_runs_on_it.md)
- [Changing a value through a shared borrow](Single_Ownership_Without_A_Count/changing_a_value_through_a_shared_borrow.md)
- [When a borrow has to wait for run time](Single_Ownership_Without_A_Count/when_a_borrow_has_to_wait_for_run_time.md)
- [Handles instead of back-references](Single_Ownership_Without_A_Count/handles_instead_of_back_references.md)
- [Threads that borrow shared data](Single_Ownership_Without_A_Count/threads_that_borrow_shared_data.md)
- [What single ownership does without](Single_Ownership_Without_A_Count/what_single_ownership_does_without.md)
- [The guards](Single_Ownership_Without_A_Count/the_guards.md)

## Modules, imports and visibility

One file is one module. There are no headers and no forward declarations: the compiler resolves a module's own names in any order, and `pub` decides what anyone else can see.

- [Declaration order does not matter](Modules_Imports_And_Visibility/declaration_order_does_not_matter.md)
- [`pub`](Modules_Imports_And_Visibility/pub.md)
- [Importing](Modules_Imports_And_Visibility/importing.md)
- [Namespaces, and names that collide](Modules_Imports_And_Visibility/namespaces_and_names_that_collide.md)
- [Packages and `.rul` libraries](Modules_Imports_And_Visibility/packages_and_rul_libraries.md)
- [What is in a `.rul`](Modules_Imports_And_Visibility/what_is_in_a_rul.md)

## Decorators

A decorator is attached to a declaration. The compiler's own are written `#name` or `#name(arguments)` and change how code is compiled, checked or exposed; one the program declares is written `@name` and runs code of the program's own. The sigil tells a reader which is which at a glance.

- [Safety decorators](Decorators/safety_decorators.md)
- [`#alias` and `#as`](Decorators/alias_and_as.md)
- [Code generation decorators](Decorators/code_generation_decorators.md)
- [Decorators you write yourself](Decorators/decorators_you_write_yourself.md)
- [`#alias`: a second name](Decorators/alias_a_second_name.md)
- [`#type`: what a file produces](Decorators/type_what_a_file_produces.md)
- [`#link` and `#linkpath`: what a file needs](Decorators/link_and_linkpath_what_a_file_needs.md)
- [Exporting to C](Decorators/exporting_to_c.md)
- [Spelling and placement](Decorators/spelling_and_placement.md)

## Calling C

An `extern "C"` block declares functions that exist somewhere else. There is no marshalling layer and no generated glue: Rune's scalar types are C's scalar types.

- [Declaring foreign functions](Calling_C/declaring_foreign_functions.md)
- [Structs](Calling_C/structs.md)
- [#Convention("C")](Calling_C/convention_c.md)
- [Pointers and out-parameters](Calling_C/pointers_and_out_parameters.md)
- [Callbacks](Calling_C/callbacks.md)
- [Strings across the boundary](Calling_C/strings_across_the_boundary.md)
- [When C's name is one you want](Calling_C/when_c_s_name_is_one_you_want.md)
- [Wrapping a descriptor](Calling_C/wrapping_a_descriptor.md)
- [Linking](Calling_C/linking.md)
- [Packaging a C half](Calling_C/packaging_a_c_half.md)
- [Bindings from a header: `rune ffi`](Calling_C/bindings_from_a_header_rune_ffi.md)
- [Being called from C](Calling_C/being_called_from_c.md)
- [C++ is its own block](Calling_C/c_plus_plus_is_its_own_block.md)

## Calling C++

An `extern "C++"` block declares what a C++ library exports. The compiler then does what a C++ compiler does at the call: spells the symbol the way the Itanium ABI spells it, and passes each argument the way that target's C++ ABI passes it. No `extern "C"` shim, no generated bindings.

- [C++'s own scalar names](Calling_C_Plus_Plus/c_plus_plus_s_own_scalar_names.md)
- [What crosses](Calling_C_Plus_Plus/what_crosses.md)
- [Structs, by value](Calling_C_Plus_Plus/structs_by_value.md)
- [References and out-parameters](Calling_C_Plus_Plus/references_and_out_parameters.md)
- [Classes](Calling_C_Plus_Plus/classes.md)
- [Making one: `std::cxx`](Calling_C_Plus_Plus/making_one_std_cxx.md)
- [Templates](Calling_C_Plus_Plus/templates.md)
- [Enums and variables](Calling_C_Plus_Plus/enums_and_variables.md)
- [Renaming, and operators](Calling_C_Plus_Plus/renaming_and_operators.md)
- [Linking](Calling_C_Plus_Plus/linking.md)
- [What does not cross](Calling_C_Plus_Plus/what_does_not_cross.md)

## Calling Rust

A Rust crate is a dependency like any other: name it with `cargo = "..."` in `[dependencies]`, and `import` it. Cargo builds it; `rune` reads what it exports over the C ABI into a Rune module.

- [A crate as a dependency](Calling_Rust/a_crate_as_a_dependency.md)
- [What is bound](Calling_Rust/what_is_bound.md)
- [Bindings on their own: `rune ffi rust`](Calling_Rust/bindings_on_their_own_rune_ffi_rust.md)
- [Ownership across the line](Calling_Rust/ownership_across_the_line.md)

## The standard library

Small on purpose, and written in Rune over a C runtime you can read in an afternoon. `io`, `option`, `result`, `math` and `process` are what most programs touch; `mem` and `collections` are there for code that has to manage its own storage, `iter` is what `for` dispatches through, `any` is what a value of unknown type is asked about, `reflect` is what the compiler is asked about a type, `thread` is how a program does more than one thing at once and `task` how one thread keeps several things in progress, `net` is TCP in the shape `io`'s stream marks already describe, `fmt` is what a format string expands into, and `testing` is what a file under `tests/` reports through. `env`, `random`, `hash`, `json` and `cli` are the everyday things a program wants from outside itself — its environment, a number nobody can predict, a checksum, a document, its command line — and `time` keeps a calendar as well as a clock.

- [std::thread](The_Standard_Library/std_thread.md)
- [std::task](The_Standard_Library/std_task.md)
- [std::atomic](The_Standard_Library/std_atomic.md)
- [std::time](The_Standard_Library/std_time.md)
- [std::reflect](The_Standard_Library/std_reflect.md)
- [std::fmt](The_Standard_Library/std_fmt.md)
- [std::io](The_Standard_Library/std_io.md)
- [std::testing](The_Standard_Library/std_testing.md)
- [std::io: files](The_Standard_Library/std_io_files.md)
- [std::io: bytes and streams](The_Standard_Library/std_io_bytes_and_streams.md)
- [std::net](The_Standard_Library/std_net.md)
- [std::option](The_Standard_Library/std_option.md)
- [std::result](The_Standard_Library/std_result.md)
- [std::iter](The_Standard_Library/std_iter.md)
- [std::any](The_Standard_Library/std_any.md)
- [std::math](The_Standard_Library/std_math.md)
- [std::process](The_Standard_Library/std_process.md)
- [std::build](The_Standard_Library/std_build.md)
- [std::env](The_Standard_Library/std_env.md)
- [std::arch](The_Standard_Library/std_arch.md)
- [std::random](The_Standard_Library/std_random.md)
- [std::hash](The_Standard_Library/std_hash.md)
- [std::json](The_Standard_Library/std_json.md)
- [std::cli](The_Standard_Library/std_cli.md)
- [std::mem](The_Standard_Library/std_mem.md)
- [std::mem: Buffer](The_Standard_Library/std_mem_buffer.md)
- [std::dictionary](The_Standard_Library/std_dictionary.md)
- [std::collections](The_Standard_Library/std_collections.md)
- [std::collections::slice](The_Standard_Library/std_collections_slice.md)
- [std::collections::vector](The_Standard_Library/std_collections_vector.md)
- [Writing an allocator](The_Standard_Library/writing_an_allocator.md)
- [Built-in methods and the `$` sigil](The_Standard_Library/built_in_methods_and_the_dollar_sigil.md)

## Testing

A test is an ordinary program. `rune test` builds and runs every file under `tests/`, and the exit status is the verdict — which is what `std::testing` produces for you.

- [The checks](Testing/the_checks.md)
- [Running them](Testing/running_them.md)

## Documentation

`rune doc` writes one page out of two halves that do not know about each other: what the compiler saw, and what you wrote under `docs/`.

- [The two halves](Documentation/the_two_halves.md)
- [What the reference reports](Documentation/what_the_reference_reports.md)
- [Reading it](Documentation/reading_it.md)
- [Running it](Documentation/running_it.md)

## Builders

A tree of things is awkward to write as nested calls: the punctuation piles up at the end and the shape of the thing is lost in it. A builder block gives the shape back — and it is not special syntax for one type, but a rewrite anything can opt into.

- [The block form](Builders/the_block_form.md)
- [The mark](Builders/the_mark.md)
- [Telling it from a struct literal](Builders/telling_it_from_a_struct_literal.md)
- [Builders nest](Builders/builders_nest.md)

## Macros

A macro is a rewrite from one run of tokens to another, chosen by pattern. Expansion happens before anything is parsed, so a macro can stand for whatever the grammar accepts — an expression, a run of statements, a declaration.

- [Patterns](Macros/patterns.md)
- [Folding a repetition](Macros/folding_a_repetition.md)
- [How an expansion is spliced](Macros/how_an_expansion_is_spliced.md)
- [The macros the compiler supplies](Macros/the_macros_the_compiler_supplies.md)
- [stringify!](Macros/stringify.md)
- [Macros that declare](Macros/macros_that_declare.md)
- [When an expansion is wrong](Macros/when_an_expansion_is_wrong.md)
- [Visibility](Macros/visibility.md)
- [The macros the standard library provides](Macros/the_macros_the_standard_library_provides.md)
- [What is reported](Macros/what_is_reported.md)
- [When a pattern is not enough](Macros/when_a_pattern_is_not_enough.md)
- [Declaring a macro anywhere](Macros/declaring_a_macro_anywhere.md)
- [Macros a library exports](Macros/macros_a_library_exports.md)
- [When the package is built again](Macros/when_the_package_is_built_again.md)
- [What a macro is given](Macros/what_a_macro_is_given.md)
- [Taking input apart by a pattern](Macros/taking_input_apart_by_a_pattern.md)
- [What is reported, for these](Macros/what_is_reported_for_these.md)

## Reading a diagnostic

Every message has the same anatomy, so once you can read one you can read all of them. The output on this page is captured from the compiler, not transcribed.

- [The parts](Reading_A_Diagnostic/the_parts.md)
- [Warnings](Reading_A_Diagnostic/warnings.md)
- [Error codes](Reading_A_Diagnostic/error_codes.md)

## The toolchain

`runec` compiles files. `rune` manages packages and calls `runec`. For anything bigger than one file, use `rune`.

- [runec](The_Toolchain/runec.md)
- [Where the time goes](The_Toolchain/where_the_time_goes.md)
- [What ends up in the artefact](The_Toolchain/what_ends_up_in_the_artefact.md)
- [Debug builds](The_Toolchain/debug_builds.md)
- [rune](The_Toolchain/rune.md)
- [Editor support](The_Toolchain/editor_support.md)
- [How a build decides what to do](The_Toolchain/how_a_build_decides_what_to_do.md)
- [Producing something other than a program](The_Toolchain/producing_something_other_than_a_program.md)
- [Package layout](The_Toolchain/package_layout.md)
- [Rune.toml](The_Toolchain/rune_toml.md)
- [Dependencies](The_Toolchain/dependencies.md)
- [Packages from a registry](The_Toolchain/packages_from_a_registry.md)

## Packages and registries

How a package is made, how one is used, and how a registry is run. A package is a directory with a `Rune.toml`; a registry is a directory of static files that any `rune` can install from. `examples/package/` is all of this worked through: seven packages, two programs and the registry that serves them.

- [Making a package](Packages_And_Registries/making_a_package.md)
- [What a version promises](Packages_And_Registries/what_a_version_promises.md)
- [Using a package](Packages_And_Registries/using_a_package.md)
- [Local units](Packages_And_Registries/local_units.md)
- [Workspaces](Packages_And_Registries/workspaces.md)
- [Which registry](Packages_And_Registries/which_registry.md)
- [Making a registry](Packages_And_Registries/making_a_registry.md)
- [Releasing a new version](Packages_And_Registries/releasing_a_new_version.md)
- [The example ecosystem](Packages_And_Registries/the_example_ecosystem.md)

## Threads and sharing

`std::thread` runs more than one thing at once. The rule the whole module rests on is that **threads share nothing they can change** — and the compiler, not the programmer, is what checks it.

- [Two questions](Threads_And_Sharing/two_questions.md)
- [Running something](Threads_And_Sharing/running_something.md)
- [When it will not compile](Threads_And_Sharing/when_it_will_not_compile.md)
- [Shared mutable state](Threads_And_Sharing/shared_mutable_state.md)
- [Asking directly](Threads_And_Sharing/asking_directly.md)
- [Sharing a value](Threads_And_Sharing/sharing_a_value.md)
- [Channels](Threads_And_Sharing/channels.md)
- [Counters and flags](Threads_And_Sharing/counters_and_flags.md)
- [Waiting](Threads_And_Sharing/waiting.md)
- [What reference counting costs](Threads_And_Sharing/what_reference_counting_costs.md)
- [Where this works](Threads_And_Sharing/where_this_works.md)
- [What is not here](Threads_And_Sharing/what_is_not_here.md)

## Tasks and futures

`async fn` and `.await`: one thread doing several things at once. Where threads run *at the same time* and the compiler checks what may cross between them, tasks take turns on one thread — so they share whatever they like, and the question is only who runs next.

- [A task is a stack](Tasks_And_Futures/a_task_is_a_stack.md)
- [Where `.await` may be written](Tasks_And_Futures/where_await_may_be_written.md)
- [Blocks and closures](Tasks_And_Futures/blocks_and_closures.md)
- [What a task may take](Tasks_And_Futures/what_a_task_may_take.md)
- [Sharing between tasks](Tasks_And_Futures/sharing_between_tasks.md)
- [Sleeping, waiting, and letting others run](Tasks_And_Futures/sleeping_waiting_and_letting_others_run.md)
- [Cancellation and timeouts](Tasks_And_Futures/cancellation_and_timeouts.md)
- [Work on other threads](Tasks_And_Futures/work_on_other_threads.md)
- [Sockets driven by tasks](Tasks_And_Futures/sockets_driven_by_tasks.md)
- [What it costs](Tasks_And_Futures/what_it_costs.md)
- [What is not here](Tasks_And_Futures/what_is_not_here.md)

## Build scripts

When a package needs more than compiling and linking — a flag worked out from the machine, a library found at build time, a kernel laid out as a disk image after the link — it says so in Rune, in a `build.rune` beside its `Rune.toml`.

- [Two phases](Build_Scripts/two_phases.md)
- [What it is told](Build_Scripts/what_it_is_told.md)
- [How it answers](Build_Scripts/how_it_answers.md)
- [Linking it yourself](Build_Scripts/linking_it_yourself.md)
- [When it runs](Build_Scripts/when_it_runs.md)

## Cross compilation

Building for a machine that is not the one you are on. The compiler already emits code for any target LLVM knows; what a cross build needs beyond that is a toolchain to link with. For the common targets `rune` finds that toolchain itself; for the rest, the manifest names it.

- [Foreign targets](Cross_Compilation/foreign_targets.md)
- [WebAssembly](Cross_Compilation/webassembly.md)
- [A target triple](Cross_Compilation/a_target_triple.md)
- [Naming a target in the manifest](Cross_Compilation/naming_a_target_in_the_manifest.md)
- [Where the output goes](Cross_Compilation/where_the_output_goes.md)
- [The runtime](Cross_Compilation/the_runtime.md)
- [Raw targets](Cross_Compilation/raw_targets.md)
- [Running what you built](Cross_Compilation/running_what_you_built.md)
- [C and C++ sources](Cross_Compilation/c_and_c_plus_plus_sources.md)
- [What does not cross](Cross_Compilation/what_does_not_cross.md)
- [32-bit targets](Cross_Compilation/32_bit_targets.md)

## Bare metal

Programs with nothing underneath them: a kernel, a boot loader, firmware. The language is the same one — the checks, the borrow checker, classes and optionals included — and what the generated code needs of a runtime is Rune compiled into the program, asking the program for the three things only it can know.

- [A freestanding program](Bare_Metal/a_freestanding_program.md)
- [Strings](Bare_Metal/strings.md)
- [The hooks](Bare_Metal/the_hooks.md)
- [Safety on bare metal](Bare_Metal/safety_on_bare_metal.md)
- [What the freestanding runtime provides](Bare_Metal/what_the_freestanding_runtime_provides.md)
- [A minimal runtime](Bare_Metal/a_minimal_runtime.md)
- [Starting without main](Bare_Metal/starting_without_main.md)
- [#weak](Bare_Metal/weak.md)
- [Bare-metal targets](Bare_Metal/bare_metal_targets.md)
- [Your own toolchain](Bare_Metal/your_own_toolchain.md)
- [Tables in the image](Bare_Metal/tables_in_the_image.md)

## Conditional compilation

`#Config(...)` decides whether a declaration exists at all. It is answered before anything is checked, so what it rules out is not merely unused — it is gone, and may name types and foreign symbols that exist on no other target.

- [`#Config`](Conditional_Compilation/config.md)
- [What a condition can ask](Conditional_Compilation/what_a_condition_can_ask.md)
- [Conditions on members](Conditional_Compilation/conditions_on_members.md)
- [Keys with a value of your own](Conditional_Compilation/keys_with_a_value_of_your_own.md)
- [Choosing a dependency's configuration](Conditional_Compilation/choosing_a_dependency_s_configuration.md)
- [Features and dependencies](Conditional_Compilation/features_and_dependencies.md)

## Inline assembly

`std::asm` hands instructions to the assembler as written. It is the least portable thing in the language and the compiler checks none of it.

- [Two calls](Inline_Assembly/two_calls.md)
- [Operands and constraints](Inline_Assembly/operands_and_constraints.md)
- [Which one to reach for](Inline_Assembly/which_one_to_reach_for.md)

## Grammar and limits

A condensed grammar for the whole language, the operator precedence table, every keyword, and an honest list of what is not implemented yet.

- [Declarations](Grammar_And_Limits/declarations.md)
- [Statements](Grammar_And_Limits/statements.md)
- [Patterns](Grammar_And_Limits/patterns.md)
- [Types](Grammar_And_Limits/types.md)
- [Operator precedence](Grammar_And_Limits/operator_precedence.md)
- [Keywords](Grammar_And_Limits/keywords.md)
- [Not implemented yet](Grammar_And_Limits/not_implemented_yet.md)
