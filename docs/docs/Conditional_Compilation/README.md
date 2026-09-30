# Conditional compilation

`@Config(...)` decides whether a declaration exists at all. It is answered before anything is checked, so what it rules out is not merely unused — it is gone, and may name types and foreign symbols that exist on no other target.

## Pages

- [`@Config`](config.md)
- [What a condition can ask](what_a_condition_can_ask.md)
- [Conditions on members](conditions_on_members.md)
- [Keys with a value of your own](keys_with_a_value_of_your_own.md)
- [Choosing a dependency's configuration](choosing_a_dependency_s_configuration.md)
- [Features and dependencies](features_and_dependencies.md)
