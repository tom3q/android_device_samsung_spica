/*
 * Copyright (C) 2012 Tomasz Figa <tomasz.figa at gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#define LOG_TAG		"CrashGuard"

#include <cutils/log.h>

#define GUARD_FILE	"/data/.crashguard"
#define MEM_FILE	"/dev/mem"
#define MEM_ADDR	(0x5a700000)
#define MEM_SIZE	(1024*1024)
#define DUMP_FILE	"/data/ram_dump"

static void signal_handler(int arg)
{
	unlink(GUARD_FILE);
}

static void dump_ram_console(void)
{
	int mem_fd, out_fd;
	void *addr;
	char *ptr;
	ssize_t size, ret;

	LOGW("Detected system crash. Dumping ram console contents.");

	mem_fd = open(MEM_FILE, O_RDONLY);
	if (mem_fd < 0) {
		LOGE("Failed to open %s.", MEM_FILE);
		return;
	}

	addr = mmap(NULL, MEM_SIZE, PROT_READ, MAP_SHARED, mem_fd, MEM_ADDR);
	if (addr == MAP_FAILED) {
		LOGE("Failed to mmap %s.", MEM_FILE);
		close(mem_fd);
		return;
	}

	out_fd = open(DUMP_FILE, O_WRONLY | O_CREAT);
	if (out_fd < 0) {
		LOGE("Failed to open %s.", DUMP_FILE);
		munmap(addr, MEM_SIZE);
		close(mem_fd);
		return;
	}

	size = MEM_SIZE;
	ptr = (char *)addr;
	do {
		ret = write(out_fd, ptr, size);
		if (ret < 0) {
			LOGE("Failed to write dump data.");
			munmap(addr, MEM_SIZE);
			close(mem_fd);
			close(out_fd);
			return;
		}
		size -= ret;
		ptr += ret;
	} while (size > 0);

	munmap(addr, MEM_SIZE);
	close(mem_fd);
	close(out_fd);

	system("gzip " DUMP_FILE);

	LOGI("Dump written to %s.gz.", DUMP_FILE);
}

int main(int argc, char **argv)
{
	int fd;

	LOGI("Starting CrashGuard...");

	fd = open(GUARD_FILE, O_RDWR | O_CREAT | O_EXCL);
	if (fd < 0)
		dump_ram_console();
	else
		close(fd);

	signal(SIGTERM, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGQUIT, signal_handler);

	pause();

	LOGI("CrashGuard terminating.");

	return 0;
}
