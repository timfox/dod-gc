#include "dod_film.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int near(float a, float b, float eps)
{
	return fabsf(a - b) <= eps;
}

static const char *const sample_cast =
	"act 1\n"
	"// two soldiers cross the sand\n"
	"actor a1 allies rifleman\n"
	"key 0 0 0 -200 - run\n"
	"key 4 400 0 -200 - crouch\n"
	"key 6 400 0 -200 90 crouch\n"
	"actor a2 axis\n"
	"key 0 10 20 30 180 aim\n";

static void test_parse(void)
{
	DodFilmCast cast;
	char err[96];
	assert(DodFilm_ParseCast(&cast, sample_cast, err, sizeof(err)) == 2);
	assert(!strcmp(cast.actors[0].name, "a1"));
	assert(cast.actors[0].team == 1 && cast.actors[0].player_class == 1);
	assert(cast.actors[0].key_count == 3);
	assert(cast.actors[0].keys[0].yaw == DOD_FILM_YAW_TRAVEL);
	assert(cast.actors[0].keys[1].pose == DOD_POSE_CROUCH);
	assert(cast.actors[1].team == 2 && cast.actors[1].player_class == 0);
	assert(DodFilm_ParseCast(&cast, "act 1\n", err, sizeof(err)) == 0);
}

static void test_errors(void)
{
	DodFilmCast cast;
	char err[96];
	assert(DodFilm_ParseCast(&cast, "", err, sizeof(err)) < 0);
	assert(DodFilm_ParseCast(&cast, "cam 1\n", err, sizeof(err)) < 0);
	assert(DodFilm_ParseCast(&cast, "act 1\nkey 0 0 0 0 0 run\n", err, sizeof(err)) < 0);
	assert(strstr(err, "before any actor"));
	assert(DodFilm_ParseCast(&cast, "act 1\nactor a nazi\n", err, sizeof(err)) < 0);
	assert(DodFilm_ParseCast(&cast, "act 1\nactor a axis\nkey 0 0 0 0 0 dance\n", err, sizeof(err)) < 0);
	assert(strstr(err, "pose"));
	assert(DodFilm_ParseCast(&cast, "act 1\nactor a axis\nkey 2 0 0 0 0 run\nkey 1 0 0 0 0 run\n", err, sizeof(err)) < 0);
	assert(strstr(err, "order"));
	assert(DodFilm_ParseCast(&cast, "act 1\nactor a axis\n", err, sizeof(err)) < 0);
	assert(strstr(err, "no keys"));
	{
		char big[4096] = "act 1\n";
		int i;
		for (i = 0; i <= DOD_FILM_MAX_ACTORS; ++i)
			strcat(big, "actor x allies\nkey 0 0 0 0 0 stand\n");
		assert(DodFilm_ParseCast(&cast, big, err, sizeof(err)) < 0);
		assert(strstr(err, "too many actors"));
	}
}

static void test_fx(void)
{
	DodFilmCast cast;
	char err[96];
	int first, end;
	const char *text =
		"act 1\n"
		"fx 2.5 water 10 20 30 2\n"
		"actor a allies\nkey 0 0 0 0 - stand\n"
		"fx 1 explosion 1 2 3\n"
		"fx 1.5 tracer 0 0 0 100 0 0\n"
		"fx 0 smoke 5 5 5\n";
	assert(DodFilm_ParseCast(&cast, text, err, sizeof(err)) == 1);
	assert(cast.fx_count == 4);
	/* sorted by time */
	assert(cast.fx[0].kind == DOD_FX_KIND_SMOKE && cast.fx[1].kind == DOD_FX_KIND_EXPLOSION);
	assert(cast.fx[2].kind == DOD_FX_KIND_TRACER && near(cast.fx[2].end[0], 100, 1e-3f));
	assert(cast.fx[3].kind == DOD_FX_KIND_WATER && near(cast.fx[3].scale, 2, 1e-3f));
	assert(near(cast.fx[1].scale, 1, 1e-3f));
	/* an fx at 0 fires on the first step past it; each fires once over a run of steps */
	end = DodFilm_FxDue(&cast, -0.5f, 0.0f, &first);
	assert(first == 0 && end == 1);
	end = DodFilm_FxDue(&cast, 0.0f, 1.2f, &first);
	assert(first == 1 && end == 2);
	end = DodFilm_FxDue(&cast, 1.2f, 3.0f, &first);
	assert(first == 2 && end == 4);
	end = DodFilm_FxDue(&cast, 3.0f, 9.0f, &first);
	assert(first == end);
	assert(DodFilm_FxDue(&cast, 2.0f, 2.0f, &first) == 0);
	/* errors */
	assert(DodFilm_ParseCast(&cast, "act 1\nfx 1 blood 4 5 6 0.5\n", err, sizeof(err)) == 0);
	assert(cast.fx_count == 1 && cast.fx[0].kind == DOD_FX_KIND_BLOOD && near(cast.fx[0].scale, 0.5f, 1e-3f));
	assert(DodFilm_PoseFromName("fallen") == DOD_POSE_FALLEN && !DodFilm_PoseLoops(DOD_POSE_FALLEN));
	assert(DodFilm_ParseCast(&cast, "act 1\nfx 1 nuke 0 0 0\n", err, sizeof(err)) < 0);
	assert(strstr(err, "kind"));
	assert(DodFilm_ParseCast(&cast, "act 1\nfx 1 tracer 0 0 0\n", err, sizeof(err)) < 0);
	assert(strstr(err, "tracer"));
	assert(DodFilm_ParseCast(&cast, "act 1\nfx 1 smoke 0 0\n", err, sizeof(err)) < 0);
	/* fx only, no actors, is a valid cast */
	assert(DodFilm_ParseCast(&cast, "act 1\nfx 1 smoke 0 0 0\n", err, sizeof(err)) == 0 && cast.fx_count == 1);
	assert(DodFilm_FxFromName(DodFilm_FxName(DOD_FX_KIND_DIRT)) == DOD_FX_KIND_DIRT);
}

static void test_sample(void)
{
	DodFilmCast cast;
	DodFilmSample s;
	char err[96];
	assert(DodFilm_ParseCast(&cast, sample_cast, err, sizeof(err)) == 2);
	/* before the first key: holds it, facing the first leg */
	DodFilm_Sample(&cast.actors[0], -1.0f, &s);
	assert(s.origin[0] == 0 && near(s.yaw, 0, 1e-3f) && s.pose == DOD_POSE_RUN && !s.moving);
	/* half way along the run */
	DodFilm_Sample(&cast.actors[0], 2.0f, &s);
	assert(near(s.origin[0], 200, 1e-3f) && s.origin[2] == -200 && s.moving && near(s.speed, 100, 1e-3f));
	assert(s.pose == DOD_POSE_RUN && near(s.yaw, 0, 1e-3f));
	/* crouched in place: facing comes from the arriving leg until the explicit 90 */
	DodFilm_Sample(&cast.actors[0], 5.0f, &s);
	assert(near(s.origin[0], 400, 1e-3f) && !s.moving && s.pose == DOD_POSE_CROUCH && near(s.yaw, 0, 1e-3f));
	DodFilm_Sample(&cast.actors[0], 9.0f, &s);
	assert(near(s.yaw, 90, 1e-3f) && s.pose == DOD_POSE_CROUCH);
	/* one key: holds it */
	DodFilm_Sample(&cast.actors[1], 3.0f, &s);
	assert(s.origin[1] == 20 && near(s.yaw, 180, 1e-3f) && s.pose == DOD_POSE_AIM);
}

static void test_poses(void)
{
	int p;
	for (p = 0; p < DOD_POSE_COUNT; ++p) {
		assert(DodFilm_PoseFromName(DodFilm_PoseName((DodFilmPose)p)) == p);
		assert(DodFilm_PoseSequences((DodFilmPose)p)[0]);
	}
	assert(DodFilm_PoseFromName("moonwalk") == -1);
	assert(DodFilm_PoseHeight(DOD_POSE_STAND) == 36 && DodFilm_PoseHeight(DOD_POSE_CROUCH) == 18);
	assert(DodFilm_PoseLoops(DOD_POSE_RUN) && !DodFilm_PoseLoops(DOD_POSE_FALLEN));
	/* the gait follows the motion */
	assert(DodFilm_GaitPose(DOD_POSE_CROUCH, 1, 70.0f) == DOD_POSE_CROUCH_WALK);
	assert(DodFilm_GaitPose(DOD_POSE_CROUCH_AIM, 1, 60.0f) == DOD_POSE_CROUCH_WALK);
	assert(DodFilm_GaitPose(DOD_POSE_CROUCH, 1, 200.0f) == DOD_POSE_RUN);
	assert(DodFilm_GaitPose(DOD_POSE_CROUCH, 0, 0.0f) == DOD_POSE_CROUCH);
	assert(DodFilm_GaitPose(DOD_POSE_RUN, 0, 0.0f) == DOD_POSE_STAND);
	assert(DodFilm_GaitPose(DOD_POSE_STAND, 1, 90.0f) == DOD_POSE_WALK);
	assert(DodFilm_GaitPose(DOD_POSE_AIM, 1, 200.0f) == DOD_POSE_RUN);
	assert(DodFilm_GaitPose(DOD_POSE_PRONE, 1, 30.0f) == DOD_POSE_CRAWL);
	assert(DodFilm_GaitPose(DOD_POSE_CRAWL, 0, 0.0f) == DOD_POSE_PRONE);
	assert(DodFilm_GaitPose(DOD_POSE_FALLEN, 1, 200.0f) == DOD_POSE_FALLEN);
}

int main(void)
{
	test_parse();
	test_errors();
	test_sample();
	test_fx();
	test_poses();
	return 0;
}
