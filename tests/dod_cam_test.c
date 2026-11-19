#include "client/dod_cam.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int near(float a, float b, float eps)
{
	return fabsf(a - b) <= eps;
}

static void test_passthrough_outside_chase(void)
{
	DodCamState s;
	const float org[3] = { 1, 2, 3 }, ang[3] = { 10, 20, 0 };
	float o[3], a[3];
	memset(&s, 0, sizeof(s));
	DodCam_Smooth(&s, 4 /* in-eye */, 5, org, ang, 0.016f, o, a);
	assert(o[0] == 1 && o[2] == 3 && a[1] == 20 && !s.primed);
	DodCam_Smooth(&s, 3 /* roaming */, 5, org, ang, 0.016f, o, a);
	assert(!s.primed);
	DodCam_Smooth(NULL, 2, 5, org, ang, 0.016f, o, a);	/* no state: pass through */
	assert(o[1] == 2);
}

static void test_first_frame_snaps_then_follows(void)
{
	DodCamState s;
	float org[3] = { 0, 0, 0 }, ang[3] = { 0, 0, 0 }, o[3], a[3];
	int i;
	memset(&s, 0, sizeof(s));
	DodCam_Smooth(&s, DODCAM_CHASE_FREE, 7, org, ang, 0.016f, o, a);
	assert(o[0] == 0 && s.primed);
	org[0] = 100.0f;	/* target steps 100 units in one network update */
	DodCam_Smooth(&s, DODCAM_CHASE_FREE, 7, org, ang, 0.016f, o, a);
	assert(o[0] > 5.0f && o[0] < 25.0f);	/* partial, not a step */
	for (i = 0; i < 90; i++)		/* 1.5 s later it has caught up */
		DodCam_Smooth(&s, DODCAM_CHASE_FREE, 7, org, ang, 0.016f, o, a);
	assert(near(o[0], 100.0f, 0.5f));
}

static void test_frame_rate_independent(void)
{
	DodCamState s30, s60;
	float org[3] = { 0, 0, 0 }, ang[3] = { 0, 0, 0 }, o30[3], o60[3], a[3];
	int i;
	memset(&s30, 0, sizeof(s30));
	memset(&s60, 0, sizeof(s60));
	DodCam_Smooth(&s30, 2, 1, org, ang, 0.0333f, o30, a);
	DodCam_Smooth(&s60, 2, 1, org, ang, 0.0167f, o60, a);
	org[0] = 100.0f;
	DodCam_Smooth(&s30, 2, 1, org, ang, 0.0333f, o30, a);
	for (i = 0; i < 2; i++)
		DodCam_Smooth(&s60, 2, 1, org, ang, 0.0167f, o60, a);
	assert(near(o30[0], o60[0], 0.5f));
}

static void test_snaps(void)
{
	DodCamState s;
	float org[3] = { 0, 0, 0 }, ang[3] = { 0, 0, 0 }, o[3], a[3];
	memset(&s, 0, sizeof(s));
	DodCam_Smooth(&s, 2, 1, org, ang, 0.016f, o, a);
	org[0] = 50.0f;
	DodCam_Smooth(&s, 2, 9, org, ang, 0.016f, o, a);	/* new target */
	assert(o[0] == 50.0f);
	org[0] = 60.0f;
	DodCam_Smooth(&s, 1, 9, org, ang, 0.016f, o, a);	/* mode change */
	assert(o[0] == 60.0f);
	org[0] = 700.0f;
	DodCam_Smooth(&s, 1, 9, org, ang, 0.016f, o, a);	/* teleport / respawn */
	assert(o[0] == 700.0f);
	org[0] = 710.0f;
	DodCam_Smooth(&s, 1, 9, org, ang, 1.0f, o, a);	/* long hitch */
	assert(o[0] == 710.0f);
	org[0] = 720.0f;
	DodCam_Smooth(&s, 1, 9, org, ang, 0.0f, o, a);	/* paused frame: hold */
	assert(o[0] == 710.0f);
}

static void test_angle_wrap(void)
{
	DodCamState s;
	float org[3] = { 0, 0, 0 }, ang[3] = { 0, 170, 0 }, o[3], a[3];
	int i;
	memset(&s, 0, sizeof(s));
	DodCam_Smooth(&s, 2, 1, org, ang, 0.016f, o, a);
	ang[1] = -170.0f;	/* 20 degrees the short way, across the seam */
	DodCam_Smooth(&s, 2, 1, org, ang, 0.016f, o, a);
	assert(a[1] > 170.0f && a[1] < 180.0f + 5.0f);	/* moved forward through 180, not back through 0 */
	for (i = 0; i < 60; i++)
		DodCam_Smooth(&s, 2, 1, org, ang, 0.016f, o, a);
	assert(near(DodCam_AngleDelta(a[1], -170.0f), 0.0f, 0.5f));
	assert(near(DodCam_AngleDelta(350.0f, 10.0f), 20.0f, 0.001f));
}

static void test_level(void)
{
	float a[3] = { 120.0f, 30.0f, 25.0f };
	DodCam_Level(DODCAM_CHASE_FREE, a);
	assert(a[0] == 89.0f && a[1] == 30.0f && a[2] == 0.0f);
	a[0] = -100.0f; a[2] = -40.0f;
	DodCam_Level(DODCAM_ROAMING, a);
	assert(a[0] == -89.0f && a[2] == 0.0f);
	a[0] = 10.0f; a[2] = 12.0f;
	DodCam_Level(DODCAM_CHASE_LOCKED, a);
	assert(a[0] == 10.0f && a[2] == 0.0f);
	a[2] = 12.0f;
	DodCam_Level(4 /* in-eye keeps the player's view */, a);
	assert(a[2] == 12.0f);
}

int main(void)
{
	test_passthrough_outside_chase();
	test_first_frame_snaps_then_follows();
	test_frame_rate_independent();
	test_snaps();
	test_angle_wrap();
	test_level();
	return 0;
}
