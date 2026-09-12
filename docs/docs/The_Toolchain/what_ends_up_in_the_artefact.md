# What ends up in the artefact

Code the artefact owns — everything in the files named on the command line — is always emitted, whether or not something in it is called, because a `.rul` has to carry every public thing it declares. Code it merely carries a copy of — the standard library, an imported library's generics — is emitted only where this artefact reaches it, and what is left is dropped before the back end sees it.

Nothing is lost by that. A dropped definition is one no call, no vtable slot, no initialiser and no metadata in the module mentions, and another artefact that wants it carries its own copy. A hello-world object holds seventeen functions rather than nine hundred.
