#include <mpd/client.h>

#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

struct worker_meta {
	long long update_interval;
	bool stop;
};

static long long
current_timestamp() {
	struct timeval te;
	gettimeofday(&te, NULL);
	return te.tv_sec*1000LL + te.tv_usec/1000;
}

static int
handle_error(struct mpd_connection *c)
{
	assert(mpd_connection_get_error(c) != MPD_ERROR_SUCCESS);

	fprintf(stderr, "%s\n", mpd_connection_get_error_message(c));
	mpd_connection_free(c);
	return EXIT_FAILURE;
}

static void
get_tag(struct mpd_song *song, char *tag, enum mpd_tag_type type)
{
	const char *value;
	unsigned i = 0;
	while ((value = mpd_song_get_tag(song, type, i++)) != NULL) {
		strcpy(tag, value);
		break;
	}
}

static int
print_status(struct mpd_connection *conn)
{
	struct mpd_status *status;
	struct mpd_song *song;

	mpd_command_list_begin(conn, true);
	mpd_send_status(conn);
	mpd_send_current_song(conn);
	mpd_command_list_end(conn);

	status = mpd_recv_status(conn);
	if (status == NULL)
		return handle_error(conn);

	const enum mpd_state state = mpd_status_get_state(status);

	const bool repeat = mpd_status_get_repeat(status);
	const bool random = mpd_status_get_random(status);

	const unsigned queue_pos = mpd_status_get_song_pos(status);
	const unsigned queue_length = mpd_status_get_queue_length(status);
	const unsigned elapsed = mpd_status_get_elapsed_time(status);
	const unsigned remaining = mpd_status_get_total_time(status) - elapsed;
	const unsigned remaining_mins = remaining / 60;
	const unsigned remaining_secs = remaining % 60;

	mpd_status_free(status);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
		return handle_error(conn);

	mpd_response_next(conn);

	char artist[256];
	char album[256];
	char title[256];

	while ((song = mpd_recv_song(conn)) != NULL) {
		get_tag(song, artist, MPD_TAG_ARTIST);
		get_tag(song, album, MPD_TAG_ALBUM);
		get_tag(song, title, MPD_TAG_TITLE);
		mpd_song_free(song);
	}

	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS ||
		!mpd_response_finish(conn))
		return handle_error(conn);

	char *play_icon;
	if (state == MPD_STATE_PLAY) {
		play_icon = "";
	} else if (state == MPD_STATE_PAUSE) {
		play_icon = "";
	} else {
		play_icon = "";
	}

	char state_icon[16];
	strcpy(state_icon, "");
	if (repeat)
		strcat(state_icon, " ");
	if (random)
		strcat(state_icon, " ");

	if (state == MPD_STATE_PLAY || state == MPD_STATE_PAUSE)
		printf("%s%s %s - %s (-%u:%02u) [%u/%u]\n",
			play_icon, state_icon, artist, title,
			remaining_mins, remaining_secs,
			queue_pos+1, queue_length);

	fflush(stdout);

	return 0;
}

void*
status_loop(void* worker_meta)
{
	struct worker_meta *meta = (struct worker_meta*)worker_meta;
	struct mpd_connection *conn = mpd_connection_new(NULL, 0, 0);

	for (;;) {
		if (meta->stop) {
			break;
		}

		long long start = current_timestamp();
		print_status(conn);
		usleep((meta->update_interval - (current_timestamp() - start)) * 1000);
	}

	mpd_connection_free(conn);
	return NULL;
}

int main(void)
{
	struct worker_meta meta = {950, false};

	pthread_t update_thread;
	if(pthread_create(&update_thread, NULL, status_loop, &meta)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	for (;;) {
		char *line;
		size_t size;

		if (getline(&line, &size, stdin) != -1) {
			struct mpd_connection *conn = mpd_connection_new(NULL, 0, 0);
			if (strcmp(line, "1\n") == 0)	{
				mpd_run_previous(conn);
			} else if (strcmp(line, "2\n") == 0) {
				mpd_run_toggle_pause(conn);
			} else if (strcmp(line, "3\n") == 0) {
				mpd_run_next(conn);
			} else if (strcmp(line, "4\n") == 0) {
				mpd_run_seek_current(conn, 3, true);
			} else if (strcmp(line, "5\n") == 0) {
				mpd_run_seek_current(conn, -3, true);
			} else {
				// ignore unrecognized input
				continue;
			}
			print_status(conn);
			mpd_connection_free(conn);
		}
	}

	if(pthread_join(update_thread, NULL)) {
		fprintf(stderr, "Error joining thread\n");
		return 2;
	}

	return 0;
}
