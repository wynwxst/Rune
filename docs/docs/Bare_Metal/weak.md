# #weak

A definition another may replace: the linker keeps a strong definition of the same symbol over it, and so does the compiler when both are in one program. It is how the freestanding runtime's defaults give way to a program's hooks, and it works the same for any `#export`ed function of your own.
