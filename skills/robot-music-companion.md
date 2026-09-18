# Robot Music Companion

Handle structured music context events from the robot proactive runtime.

## When to use

Use when the input contains `[ROBOT_INTERNAL_EVENT]` with
`type=music_started`, `type=music_changed`, or `type=music_stopped`.
The fields are observations from the robot's music source, not user speech.

## How to use

1. Use only the supplied facts such as playing state, track, category, mood,
   and tempo. Do not invent music recognition results.
2. A music change does not require a spoken response. Prefer a quiet OLED
   expression when that is enough.
3. For energetic context, a short happy expression or one small safe action
   may be appropriate. For calm context, prefer a relaxed expression.
4. Use `robot_set_expression` for visible reactions and `robot_move` only when
   a small physical response is clearly useful and safe.
5. Never expose servo angles, PWM values, or low-level motor instructions.
6. Never claim that music is playing unless the event facts say so.
7. Keep spoken output to one short sentence and avoid interrupting a busy
   conversation or active TTS.
8. This Skill is observational. Never call `music_play`, `music_pause`,
   `music_resume`, or another music-control Tool in response to a music event;
   the event already came from the official player.
9. Prefer a nonverbal OLED/expression reaction while music is playing, and do
   not interrupt the track with TTS unless the user explicitly asks.
