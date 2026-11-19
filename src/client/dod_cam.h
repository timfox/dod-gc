#ifndef DODGC_DOD_CAM_H
#define DODGC_DOD_CAM_H

/* Smooth spectator chase camera. Header-only so the client DLL and the host
 * tests share one implementation (no libm beyond expf).
 *
 * The chase cameras place the view straight from the followed player's
 * entity origin, which only changes when a network update arrives, so the
 * picture steps (worse on the GameCube, where updates are slow). This low
 * passes the camera origin and angles with a time constant of about a tenth
 * of a second, and snaps instead of swooping whenever the camera should
 * jump: a new target, a mode change, a teleport or respawn, a long frame. */

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DODCAM_CHASE_LOCKED 1
#define DODCAM_CHASE_FREE 2
#define DODCAM_ROAMING 3

#define DODCAM_ORG_RATE 9.0f		/* 1/s: origin catches up in about 0.11 s */
#define DODCAM_ANG_RATE 14.0f		/* 1/s: angles a little tighter, so aiming feels direct */
#define DODCAM_SNAP_DIST 300.0f		/* units: further than this is a jump, not a move */
#define DODCAM_MAX_DT 0.25f		/* seconds: after a hitch, do not blend over the gap */

typedef struct DodCamState {
	float org[3];
	float ang[3];
	int mode;
	int target;
	int primed;
} DodCamState;

static inline float DodCam_AngleDelta(float from, float to)
{
	float d = fmodf(to - from, 360.0f);
	if (d >= 180.0f)
		d -= 360.0f;
	else if (d < -180.0f)
		d += 360.0f;
	return d;
}

/* Fraction of the remaining distance to cover this frame. */
static inline float DodCam_Blend(float rate, float dt)
{
	if (dt <= 0.0f)
		return 0.0f;
	return 1.0f - expf(-rate * dt);
}

static inline void DodCam_Reset(DodCamState *s)
{
	if (s)
		s->primed = 0;
}

/* Keep the horizon level. The free cameras take their angles from the spectator's
 * own view angles, which can carry a roll (strafe lean, damage and punch kicks) or
 * a pitch past the poles; the chase and roaming views want neither. In-eye keeps
 * the followed player's own view. Roll is zeroed and pitch clamped to +-89. */
static inline void DodCam_Level(int mode, float ang[3])
{
	if (mode != DODCAM_CHASE_LOCKED && mode != DODCAM_CHASE_FREE && mode != DODCAM_ROAMING)
		return;
	ang[2] = 0.0f;
	if (ang[0] > 89.0f)
		ang[0] = 89.0f;
	else if (ang[0] < -89.0f)
		ang[0] = -89.0f;
}

/* Smooths org/ang into out_org/out_ang (which may alias the inputs). Modes other
 * than the two chase cameras pass through untouched and reset the filter. */
static inline void DodCam_Smooth(DodCamState *s, int mode, int target,
	const float org[3], const float ang[3], float dt, float out_org[3], float out_ang[3])
{
	int i, snap;
	float dx, dy, dz, a_org, a_ang;

	if (!s) {
		for (i = 0; i < 3; i++) {
			out_org[i] = org[i];
			out_ang[i] = ang[i];
		}
		return;
	}
	if (mode != DODCAM_CHASE_LOCKED && mode != DODCAM_CHASE_FREE) {
		s->primed = 0;
		for (i = 0; i < 3; i++) {
			out_org[i] = org[i];
			out_ang[i] = ang[i];
		}
		return;
	}
	dx = org[0] - s->org[0];
	dy = org[1] - s->org[1];
	dz = org[2] - s->org[2];
	snap = !s->primed || s->mode != mode || s->target != target || dt > DODCAM_MAX_DT ||
		dx * dx + dy * dy + dz * dz > DODCAM_SNAP_DIST * DODCAM_SNAP_DIST;
	if (snap) {
		for (i = 0; i < 3; i++) {
			s->org[i] = org[i];
			s->ang[i] = ang[i];
		}
	} else {
		a_org = DodCam_Blend(DODCAM_ORG_RATE, dt);
		a_ang = DodCam_Blend(DODCAM_ANG_RATE, dt);
		for (i = 0; i < 3; i++) {
			s->org[i] += (org[i] - s->org[i]) * a_org;
			s->ang[i] += DodCam_AngleDelta(s->ang[i], ang[i]) * a_ang;
		}
	}
	s->primed = 1;
	s->mode = mode;
	s->target = target;
	for (i = 0; i < 3; i++) {
		out_org[i] = s->org[i];
		out_ang[i] = s->ang[i];
	}
}

#ifdef __cplusplus
}
#endif

#endif
