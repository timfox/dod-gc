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
		char big[2048] = "act 1\n";
		int i;
		for (i = 0; i <= DOD_FILM_MAX_ACTORS; ++i)
			strcat(big, "actor x allies\nkey 0 0 0 0 0 stand\n");
		assert(DodFilm_ParseCast(&cast, big, err, sizeof(err)) < 0);
		assert(strstr(err, "too many actors"));
	}
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
}

int main(void)
{
	test_parse();
	test_errors();
	test_sample();
	test_poses();
	return 0;
}
