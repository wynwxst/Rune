# Calling C

An `extern "C"` block declares functions that exist somewhere else. There is no marshalling layer and no generated glue: Rune's scalar types are C's scalar types.

## Pages

- [Declaring foreign functions](declaring_foreign_functions.md)
- [Structs](structs.md)
- [Pointers and out-parameters](pointers_and_out_parameters.md)
- [Callbacks](callbacks.md)
- [Strings across the boundary](strings_across_the_boundary.md)
- [When C's name is one you want](when_c_s_name_is_one_you_want.md)
- [Wrapping a descriptor](wrapping_a_descriptor.md)
- [Linking](linking.md)
- [Packaging a C half](packaging_a_c_half.md)
- [Being called from C](being_called_from_c.md)
- [C++ is its own block](c_plus_plus_is_its_own_block.md)
