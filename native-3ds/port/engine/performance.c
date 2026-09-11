/* Visual work only: the original simulation, collision, stage platform
 * movement, RNG and fighter animation continue unchanged. Individually
 * switchable in development for same-match profiling. */
volatile unsigned mp_performance_low_poly=1;
volatile unsigned mp_performance_hide_stars=1;
volatile unsigned mp_performance_skip_reflection=1;
/* All stages: omit the extra fighter render and per-surface projected
 * shadow setup. Lighting and the visible fighters still render normally. */
volatile unsigned mp_performance_no_shadows=1;
