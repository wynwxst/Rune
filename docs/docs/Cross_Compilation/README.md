# Cross compilation

Building for a machine that is not the one you are on. The compiler already emits code for any target LLVM knows; what a cross build needs beyond that is a toolchain to link with, and that is what the manifest describes.

## Pages

- [A target triple](a_target_triple.md)
- [Naming a target in the manifest](naming_a_target_in_the_manifest.md)
- [Where the output goes](where_the_output_goes.md)
- [The runtime](the_runtime.md)
- [Running what you built](running_what_you_built.md)
- [C sources](c_sources.md)
- [What does not cross](what_does_not_cross.md)
- [32-bit targets](32_bit_targets.md)
