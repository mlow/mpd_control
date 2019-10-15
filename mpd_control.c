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
	int scroll_length;
	int scroll_index;
	char* label;
};

bool
is_utf8(const char * string)
{
    if (!string)
        return false;

    const unsigned char * bytes = (const unsigned char *)string;
    while (*bytes) {
        if( (// ASCII
             // use bytes[0] <= 0x7F to allow ASCII control characters
                bytes[0] == 0x09 ||
                bytes[0] == 0x0A ||
                bytes[0] == 0x0D ||
                (0x20 <= bytes[0] && bytes[0] <= 0x7E)
            )
        ) {
            bytes += 1;
            continue;
        }

        if( (// non-overlong 2-byte
                (0xC2 <= bytes[0] && bytes[0] <= 0xDF) &&
                (0x80 <= bytes[1] && bytes[1] <= 0xBF)
            )
        ) {
            bytes += 2;
            continue;
        }

        if( (// excluding overlongs
                bytes[0] == 0xE0 &&
                (0xA0 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF)
            ) ||
            (// straight 3-byte
                ((0xE1 <= bytes[0] && bytes[0] <= 0xEC) ||
                    bytes[0] == 0xEE ||
                    bytes[0] == 0xEF) &&
                (0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF)
            ) ||
            (// excluding surrogates
                bytes[0] == 0xED &&
                (0x80 <= bytes[1] && bytes[1] <= 0x9F) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF)
            )
        ) {
            bytes += 3;
            continue;
        }

        if( (// planes 1-3
                bytes[0] == 0xF0 &&
                (0x90 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
                (0x80 <= bytes[3] && bytes[3] <= 0xBF)
            ) ||
            (// planes 4-15
                (0xF1 <= bytes[0] && bytes[0] <= 0xF3) &&
                (0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
                (0x80 <= bytes[3] && bytes[3] <= 0xBF)
            ) ||
            (// plane 16
                bytes[0] == 0xF4 &&
                (0x80 <= bytes[1] && bytes[1] <= 0x8F) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
                (0x80 <= bytes[3] && bytes[3] <= 0xBF)
            )
        ) {
            bytes += 4;
            continue;
        }

        return false;
    }

    return true;
}

static long long
current_timestamp() {
	struct timeval te;
	gettimeofday(&te, NULL);
	return te.tv_sec*1000LL + te.tv_usec/1000;
}

static void
scroll_text(char *text_to_scroll, int text_len, char *buffer, int *index,
			const int max_length) {
	char last_utf8[5] = { '\0' };
	do {
		if (*index - text_len > 0) {
			*index %= text_len;
		}

		const int overflow = text_len - *index - max_length;
		if (overflow <= 0) {
			memcpy(buffer, &text_to_scroll[*index], text_len-*index);
			memcpy(&buffer[text_len-*index], text_to_scroll, max_length-(text_len-*index));
		} else {
			memcpy(buffer, &text_to_scroll[*index], max_length);
		}

		strcat(last_utf8, &buffer[max_length-1]);
		if (is_utf8(last_utf8)) {
			break;
		}
		*index += 1;
	} while(true);
	buffer[max_length] = '\0';

	char utf8[5] = { '\0' };
	int i = 0;
	do {
		memcpy(utf8, &text_to_scroll[*index], ++i);
	} while (!is_utf8(utf8));
	if (strlen(utf8) > 1) {
		*index += strlen(utf8)-1;
	}
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
print_status(struct mpd_connection *conn, struct worker_meta *meta)
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

	if (state == MPD_STATE_STOP || state == MPD_STATE_UNKNOWN) {
		// Early exit
		printf("\n");
		fflush(stdout);
		return 0;
	}

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

	// the full song label. would be cool if this was customizable at runtime
	char full_label[strlen(artist) + strlen(title) + 4];
	sprintf(full_label, "%s - %s", artist, title);

	// compare current label against previous update's label
	if (strcmp(meta->label, full_label) != 0) {
		// The song changed, reset scroll and update meta
		meta->scroll_index = 0;
		strcpy(meta->label, full_label);
	}

	// The label we'll actually print
	char label[meta->scroll_length+1];
	if (strlen(full_label) > meta->scroll_length) {
		// If length of full label > meta->scroll_length, we'll scroll it

		// Pad the text with a separator
		char padded_text[strlen(full_label) + 3];
		sprintf(padded_text, "%s | ", full_label);

		scroll_text(padded_text, strlen(padded_text), label,
			&meta->scroll_index, meta->scroll_length);
	} else {
		// Else we'll just print it out normally and reset the scroll index
		meta->scroll_index = 0;
		sprintf(label, "%s - %s", artist, title);
	}

	printf("%s%s %s (-%u:%02u) [%u/%u]\n",
		play_icon, state_icon, label,
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
		print_status(conn, meta);

		meta->scroll_index++;

		usleep((meta->update_interval - (current_timestamp() - start)) * 1000);
	}

	mpd_connection_free(conn);
	return NULL;
}

int main(void) {
	struct worker_meta meta = {950, false, 30, 0, malloc(1024)};

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
			print_status(conn, &meta);
			mpd_connection_free(conn);
		}
	}

	if(pthread_join(update_thread, NULL)) {
		fprintf(stderr, "Error joining thread\n");
		return 2;
	}

	return 0;
}
