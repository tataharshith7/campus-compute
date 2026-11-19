campuscompute : build/main.o build/jobs.o build/scheduler.o build/banker.o build/logger.o build/ipc.o build/workers.o build/rlimit_demo.o build/util.o
	gcc build/main.o build/jobs.o build/scheduler.o build/banker.o build/logger.o build/ipc.o build/workers.o build/rlimit_demo.o build/util.o -o campuscompute -pthread -lrt
build/main.o : src/main.c include/banker.h include/common.h include/ipc.h include/jobs.h include/logger.h include/rlimit_demo.h include/scheduler.h include/workers.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/main.c -o build/main.o
build/jobs.o : src/jobs.c include/common.h include/jobs.h include/logger.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/jobs.c -o build/jobs.o
build/scheduler.o : src/scheduler.c include/scheduler.h include/jobs.h include/logger.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/scheduler.c -o build/scheduler.o
build/banker.o : src/banker.c include/banker.h include/common.h include/logger.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/banker.c -o build/banker.o
build/logger.o : src/logger.c include/common.h include/logger.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/logger.c -o build/logger.o
build/ipc.o : src/ipc.c include/ipc.h include/jobs.h include/logger.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/ipc.c -o build/ipc.o
build/workers.o : src/workers.c include/workers.h include/logger.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/workers.c -o build/workers.o
build/rlimit_demo.o : src/rlimit_demo.c include/rlimit_demo.h include/logger.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/rlimit_demo.c -o build/rlimit_demo.o
build/util.o : src/util.c include/common.h | build
	gcc -std=c11 -Wall -Wextra -D_GNU_SOURCE -Iinclude -g -c src/util.c -o build/util.o
build :
	mkdir -p build
run : campuscompute
	./campuscompute demo
clean :
	rm -rf build campuscompute logs
test : campuscompute
	bash ./tests/smoke.sh
loop : campuscompute
	@ok=0; for i in $$(seq 20); do \
	  timeout 30 ./campuscompute logger 3 5 > /dev/null 2>&1 && ok=$$((ok+1)); done; \
	echo "logger loop: $$ok/20 runs succeeded"; [ $$ok -eq 20 ]
valgrind : campuscompute
	@for c in "compare" "banker data/banker_safe.txt" "logger 3 5" "jobs nofile"; do \
	  n=$$(valgrind -q --leak-check=full ./campuscompute $$c 2>&1 >/dev/null | grep -c '^=='); \
	  echo "valgrind [$$c]: $$n messages"; done; true
.PHONY : run clean test loop valgrind
