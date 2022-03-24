#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <poll.h>

#include <string>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

extern "C" {
#include "rtmp_client.h"
}

static int test_running = 0;

typedef struct user_test {
    int64_t video_cnt;
    int64_t audio_cnt;
    int idx;
    void *resv;
    void *other;
    char videofile[512];
    char audiofile[512];
    char url[512];
    FILE *vfs, *afs; 
    int *running;
    int timeout;
} UserTestData;

int callback_on_recv(void *contex, void *user_data, unsigned char *buffer, int length, int video)
{
    UserTestData *udata = (UserTestData *)user_data;
    FILE* out_fd = (FILE *)udata->vfs;
    FILE* aout_fd = (FILE *)udata->afs;

    if (video && !(udata->video_cnt++ % 512) ) {
        TestDbg(TEST_DBG, "[stream%d]pkt count video %ld, audio %ld\n", udata->idx, udata->video_cnt, udata->audio_cnt);
    } else if (!video && !(udata->audio_cnt++ % 512)) {
        TestDbg(TEST_DBG, "[stream%d]pkt count video %ld, audio %ld\n", udata->idx, udata->video_cnt, udata->audio_cnt);
    }
    size_t size = 0;
    if (out_fd && video) {
        size = fwrite(buffer, 1, length, out_fd);
        if(size != length)
        {
            TestDbg(TEST_DBG, "[stream%d]file write failed!\n", udata->idx);
        }
    }
    if (aout_fd && !video) {
        size = fwrite(buffer, 1, length, aout_fd);
        if(size != length)
        {
            TestDbg(TEST_DBG, "[stream%d]file write failed!\n", udata->idx);
        }
    }

    return size;
}

int callback_on_finish(void *contex, void *user_data, int status)
{
    UserTestData *udata = (UserTestData *)user_data;
    printf("[%p %p]Streaming finished status %d!\n", contex, user_data, status);
    if (udata && udata->running) *(udata->running) = 0;
    test_running = 0;
}
int callback_on_debug(const char *buffer)
{
    if (buffer) printf("%s\n", buffer);
}

RtmpClientCallback user_callback = {callback_on_recv, callback_on_finish, callback_on_debug};

void *walker_client(void *arg)
{
    UserTestData *udata = (UserTestData *)arg;
    if (udata) {
        FILE* out_fd = fopen(udata->videofile, "wb");
        FILE* aout_fd = fopen(udata->audiofile, "wb");

        udata->vfs = out_fd;
        udata->afs = aout_fd;

        TestDbg(TEST_DBG, "[stream%d]video file %s, audio file %s\n", udata->idx,
            udata->videofile, udata->audiofile);

        rtmp_client_start(udata, udata->url, udata->timeout ? udata->timeout + 3 : 0, udata->timeout, &user_callback);
    } else {
        TestDbg(TEST_DBG, "Thread start failed!\n");
    }
    
}

void *walker_doggy(void *arg)
{
    int clifd = 0;
	struct sockaddr_in dstaddr;
    int *running = (int *)arg;

	clifd = socket(AF_INET, SOCK_STREAM, 0);
	if (clifd < 0) {
		perror("[doggy]create socket");
		return -1;
	}
	int clifml = AF_INET;
	dstaddr.sin_family = AF_INET;
	dstaddr.sin_port = htons(0);
	dstaddr.sin_addr.s_addr = inet_addr("0.0.0.0");

	size_t addrlen = (clifml == AF_INET)?sizeof(dstaddr):sizeof(dstaddr);
	struct sockaddr *addrp = (struct sockaddr *)&dstaddr;
	if (bind(clifd, addrp, addrlen) < 0) {
		perror("[doggy]bind on");
		close(clifd);
		return -1;
	}

	if (listen(clifd, 50) < 0) {
		perror("[doggy]wait on");
		close(clifd);
		return -1;
	}
    char rcvbuf[5120];
    struct pollfd fds[5] = {0};
	ssize_t rcvlen = 0;
	int num = 0, cnt = 0, ret;

    for(;;) {
		num	= 0;
		memset(fds, 0, sizeof(fds));
		fds[num].fd = clifd;
		fds[num].events = POLLIN | POLLERR | POLLHUP;
		fds[num].revents = 0;
		num++;

        if (*running == 0) break;
        // if (test_running == 0) break;
        if ((*running > 1) && (cnt > *running)) break;

		ret = poll(fds, num, 3000); // 3s
		if (0 > ret) {
			if (errno == EINTR) {
				printf("[doggy]Got an EINTR (%s), ignoring...\n", strerror(errno)); 
				continue;
			} else {
				break;
			}
		} else if (0 == ret) {
			printf("[doggy]Wait guest timeout %d %d!\n", test_running, *running); 
            cnt++;
			continue;
		}
        // TODO: something
        
    }

	return NULL;
    
}

int main(int argc, char* argv[])
{
    pthread_t watch_doggy;
    pthread_t *walkers_thr;
    int ret = 0, num = 0, i = 0;
    void *retval;
    const char *stream_url = "rtmp://10.200.198.75:1935/live/test00";
    int running = 0, timeout = 0;
    void *contex = NULL;

    num = 1;

    if (argc > 3) {
        num = atoi(argv[1]);
        timeout = atoi(argv[2]);
        stream_url = argv[3];
    } else if (argc > 2) {
        num = atoi(argv[1]);
        stream_url = argv[2];
    } else if (argc > 1) {
        num = atoi(argv[1]);
    } else {
        num = 1;
    }
    if (num < 0) {
        TestDbg(TEST_DBG, "Param error!\n");
        exit(0);
    }

    test_running = 1;

    walkers_thr = (pthread_t *)malloc(sizeof(pthread_t));

    for (i = 0; i < num; i++) {
        UserTestData *udata = new UserTestData;

        if (udata) {
            
            memset(udata->videofile, 0, sizeof(udata->videofile));
            snprintf(udata->videofile, sizeof(udata->videofile) -1, "/data/user%d.h264", i);
            memset(udata->audiofile, 0, sizeof(udata->audiofile));
            snprintf(udata->audiofile, sizeof(udata->audiofile) -1, "/data/user%d.aac", i);
            memset(udata->url, 0, sizeof(udata->url));
            snprintf(udata->url, sizeof(udata->url) -1, "%s", stream_url);
            udata->video_cnt = 0;
            udata->audio_cnt = 0;
            udata->idx = i;
            udata->vfs = NULL;
            udata->afs = NULL;
            udata->running = &test_running;
            udata->timeout = timeout;

            ret = pthread_create(&walkers_thr[i], NULL, walker_client, (void *)udata);
        }
    }

    if (num == 0) {
        
        UserTestData *udata = new UserTestData;

        if (udata) {
            FILE* out_fd = fopen("/data/user.h264", "wb");
            FILE* aout_fd = fopen("/data/user.aac", "wb");

            memset(udata->videofile, 0, sizeof(udata->videofile));
            snprintf(udata->videofile, sizeof(udata->videofile) -1, "/data/user.h264");
            memset(udata->audiofile, 0, sizeof(udata->audiofile));
            snprintf(udata->audiofile, sizeof(udata->audiofile) -1, "/data/user.aac");
            memset(udata->url, 0, sizeof(udata->url));
            snprintf(udata->url, sizeof(udata->url) -1, "%s", stream_url);

            udata->vfs = out_fd;
            udata->afs = aout_fd;
            udata->video_cnt = 0;
            udata->audio_cnt = 0;
            udata->idx = 0;
            udata->running = &test_running;
            udata->timeout = 0;

            TestDbg(TEST_DBG, "[stream%d]video file %s, audio file %s\n", udata->idx,
                udata->videofile, udata->audiofile);

            contex = rtmp_client_start(udata, udata->url, udata->timeout ? udata->timeout + 3 : 0,
                udata->timeout, &user_callback);

            test_running = 5;
        }

        
    }

    ret = pthread_create(&watch_doggy, NULL, walker_doggy, &test_running);
    if (ret < 0) {
        return 0;
    }
    
    pthread_join(watch_doggy, &retval);

    if (contex) {
        rtmp_client_stop(contex);
    }
    return retval;
}
