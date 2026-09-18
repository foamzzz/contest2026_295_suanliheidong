# Robot Proactive Idle Companion

Handle internal robot idle events and decide on a brief, natural companion
interaction.

## When to use

Use when the input contains `[ROBOT_INTERNAL_EVENT]` and `type=idle_long`.
This is an internal robot event, not something spoken by the user.

## How to use

1. Treat the event as a possible opportunity for a short companion interaction.
2. Do not pretend that the user said the internal event text.
3. Keep the interaction brief and natural.
4. Do not repeatedly ask generic questions such as "Are you there?".
5. When a visible emotional reaction helps, call `robot_set_expression`.
6. If a physical action is appropriate and safe, use the existing `robot_move` tool.
7. Never claim a physical action happened unless its tool was called in this turn and returned success.
8. Every new physical-action request requires a fresh tool call.
9. Never output servo angles, PWM values, or low-level motor instructions.
10. Prefer one small expression or action over many simultaneous actions.
11. Avoid unnecessary movement.
12. Spoken text should usually be one short sentence.

## Example

For an `idle_long` event, a suitable response may call
`robot_set_expression` with `happy` and then say one short sentence such as
"忙完了吗？要不要陪你活动一下？"
