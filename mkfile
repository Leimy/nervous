</$objtype/mkfile

BIN=$home/bin/$objtype

TARG=nervous
OFILES=\
	cmd/nervous/main.$O\
	lib/alloc.$O\
	lib/ast.$O\
	lib/bcread.$O\
	lib/bytecode.$O\
	lib/compile.$O\
	lib/exec.$O\
	lib/format.$O\
	lib/gc.$O\
	lib/lex.$O\
	lib/parse.$O\
	lib/patbc.$O\
	lib/patcompile.$O\
	lib/pattern.$O\
	lib/process.$O\
	lib/sched.$O\
	lib/value.$O\
	lib/verify.$O\
	lib/vm.$O\

HFILES=include/nervous.h include/nvalloc.h include/nvbc.h include/nvvm.h include/nvexec.h include/nvpat.h include/nvpatbc.h include/nvcompile.h include/nvproc.h include/nvsched.h

all:V: $TARG

benchmarks:V: all perftest

perftest: bench/perftest.$O lib/alloc.$O lib/ast.$O lib/lex.$O lib/parse.$O lib/patcompile.$O lib/patbc.$O lib/compile.$O lib/verify.$O lib/pattern.$O lib/process.$O lib/sched.$O lib/value.$O lib/vm.$O lib/exec.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o bench/perftest $prereq

bench/perftest.$O: bench/perftest.c include/nervous.h include/nvbc.h include/nvvm.h include/nvexec.h include/nvproc.h include/nvsched.h include/nvcompile.h
	$CC $CFLAGS -o bench/perftest.$O bench/perftest.c

tests:V: all patterntest parsepatterntest patternbctest patterncompiletest processtest exectest schedtest iotest r2test memorytest automatictest

automatictest: tests/memory/autotest.$O lib/alloc.$O lib/ast.$O lib/lex.$O lib/parse.$O lib/patcompile.$O lib/patbc.$O lib/compile.$O lib/verify.$O lib/pattern.$O lib/process.$O lib/sched.$O lib/value.$O lib/vm.$O lib/exec.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/memory/autotest $prereq

tests/memory/autotest.$O: tests/memory/autotest.c include/nervous.h include/nvbc.h include/nvvm.h include/nvexec.h include/nvproc.h include/nvsched.h include/nvcompile.h
	$CC $CFLAGS -o tests/memory/autotest.$O tests/memory/autotest.c

memorytest: tests/memory/gctest.$O lib/gc.$O lib/exec.$O lib/value.$O lib/bytecode.$O
	$LD $LDFLAGS -o tests/memory/gctest $prereq

tests/memory/gctest.$O: tests/memory/gctest.c include/nvbc.h include/nvvm.h include/nvexec.h
	$CC $CFLAGS -o tests/memory/gctest.$O tests/memory/gctest.c

lib/gc.$O: lib/gc.c include/nvbc.h include/nvvm.h
	$CC $CFLAGS -o lib/gc.$O lib/gc.c

patterntest: tests/pattern/ptest.$O lib/pattern.$O lib/value.$O lib/vm.$O lib/exec.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/pattern/ptest $prereq

parsepatterntest: tests/pattern/parsetest.$O lib/alloc.$O lib/ast.$O lib/lex.$O lib/parse.$O lib/patcompile.$O lib/pattern.$O lib/value.$O lib/vm.$O lib/exec.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/pattern/parsetest $prereq

patternbctest: tests/pattern/bctest.$O lib/alloc.$O lib/patbc.$O lib/verify.$O lib/bytecode.$O
	$LD $LDFLAGS -o tests/pattern/bctest tests/pattern/bctest.$O lib/alloc.$O lib/patbc.$O lib/verify.$O lib/bytecode.$O

processtest: tests/process/ptest.$O lib/process.$O lib/pattern.$O lib/value.$O lib/vm.$O lib/exec.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/process/ptest $prereq

exectest: tests/process/exectest.$O lib/exec.$O lib/value.$O lib/vm.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/process/exectest $prereq

schedtest: tests/process/schedtest.$O lib/sched.$O lib/process.$O lib/pattern.$O lib/exec.$O lib/value.$O lib/vm.$O lib/verify.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/process/schedtest $prereq

iotest: tests/process/iotest.$O lib/sched.$O lib/process.$O lib/pattern.$O lib/exec.$O lib/value.$O lib/vm.$O lib/verify.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/process/iotest $prereq

r2test: tests/process/r2test.$O lib/alloc.$O lib/ast.$O lib/lex.$O lib/parse.$O lib/patcompile.$O lib/patbc.$O lib/compile.$O lib/verify.$O lib/pattern.$O lib/process.$O lib/sched.$O lib/value.$O lib/vm.$O lib/exec.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/process/r2test $prereq

patterncompiletest: tests/pattern/compiletest.$O lib/alloc.$O lib/ast.$O lib/lex.$O lib/parse.$O lib/patcompile.$O lib/patbc.$O lib/compile.$O lib/verify.$O lib/pattern.$O lib/value.$O lib/vm.$O lib/exec.$O lib/bytecode.$O lib/gc.$O
	$LD $LDFLAGS -o tests/pattern/compiletest $prereq

$TARG: $OFILES
	$LD $LDFLAGS -o $target $OFILES

tests/process/ptest.$O: tests/process/ptest.c include/nvbc.h include/nvvm.h include/nvexec.h include/nvpat.h include/nvproc.h
	$CC $CFLAGS -o tests/process/ptest.$O tests/process/ptest.c

tests/process/exectest.$O: tests/process/exectest.c include/nvbc.h include/nvvm.h include/nvexec.h
	$CC $CFLAGS -o tests/process/exectest.$O tests/process/exectest.c

tests/process/schedtest.$O: tests/process/schedtest.c include/nvbc.h include/nvvm.h include/nvexec.h include/nvproc.h include/nvsched.h
	$CC $CFLAGS -o tests/process/schedtest.$O tests/process/schedtest.c

tests/process/iotest.$O: tests/process/iotest.c include/nvbc.h include/nvvm.h include/nvexec.h include/nvproc.h include/nvsched.h
	$CC $CFLAGS -o tests/process/iotest.$O tests/process/iotest.c

tests/process/r2test.$O: tests/process/r2test.c include/nervous.h include/nvbc.h include/nvvm.h include/nvcompile.h include/nvexec.h include/nvproc.h include/nvsched.h
	$CC $CFLAGS -o tests/process/r2test.$O tests/process/r2test.c

tests/pattern/ptest.$O: tests/pattern/ptest.c include/nvbc.h include/nvvm.h include/nvpat.h
	$CC $CFLAGS -o tests/pattern/ptest.$O tests/pattern/ptest.c

tests/pattern/parsetest.$O: tests/pattern/parsetest.c include/nervous.h include/nvbc.h include/nvvm.h include/nvpat.h
	$CC $CFLAGS -o tests/pattern/parsetest.$O tests/pattern/parsetest.c

tests/pattern/bctest.$O: tests/pattern/bctest.c include/nvbc.h include/nvvm.h include/nvpat.h include/nvpatbc.h
	$CC $CFLAGS -o tests/pattern/bctest.$O tests/pattern/bctest.c

tests/pattern/compiletest.$O: tests/pattern/compiletest.c include/nervous.h include/nvbc.h include/nvvm.h include/nvcompile.h
	$CC $CFLAGS -o tests/pattern/compiletest.$O tests/pattern/compiletest.c

cmd/nervous/main.$O: cmd/nervous/main.c include/nervous.h include/nvbc.h include/nvvm.h include/nvexec.h include/nvcompile.h include/nvproc.h include/nvsched.h
	$CC $CFLAGS -o cmd/nervous/main.$O cmd/nervous/main.c

lib/alloc.$O: lib/alloc.c include/nvalloc.h
	$CC $CFLAGS -o lib/alloc.$O lib/alloc.c

lib/ast.$O: lib/ast.c include/nervous.h
	$CC $CFLAGS -o lib/ast.$O lib/ast.c

lib/bcread.$O: lib/bcread.c include/nvbc.h
	$CC $CFLAGS -o lib/bcread.$O lib/bcread.c

lib/bytecode.$O: lib/bytecode.c include/nvbc.h
	$CC $CFLAGS -o lib/bytecode.$O lib/bytecode.c

lib/compile.$O: lib/compile.c include/nervous.h include/nvbc.h include/nvvm.h include/nvpat.h include/nvpatbc.h include/nvcompile.h include/nvalloc.h
	$CC $CFLAGS -o lib/compile.$O lib/compile.c

lib/exec.$O: lib/exec.c include/nvbc.h include/nvvm.h include/nvexec.h
	$CC $CFLAGS -o lib/exec.$O lib/exec.c

lib/format.$O: lib/format.c include/nervous.h
	$CC $CFLAGS -o lib/format.$O lib/format.c

lib/lex.$O: lib/lex.c include/nervous.h
	$CC $CFLAGS -o lib/lex.$O lib/lex.c

lib/parse.$O: lib/parse.c include/nervous.h
	$CC $CFLAGS -o lib/parse.$O lib/parse.c

lib/patbc.$O: lib/patbc.c include/nvbc.h include/nvvm.h include/nvpat.h include/nvpatbc.h include/nvalloc.h
	$CC $CFLAGS -o lib/patbc.$O lib/patbc.c

lib/patcompile.$O: lib/patcompile.c include/nervous.h include/nvbc.h include/nvvm.h include/nvpat.h include/nvalloc.h
	$CC $CFLAGS -o lib/patcompile.$O lib/patcompile.c

lib/pattern.$O: lib/pattern.c include/nvbc.h include/nvvm.h include/nvpat.h
	$CC $CFLAGS -o lib/pattern.$O lib/pattern.c

lib/process.$O: lib/process.c include/nvbc.h include/nvvm.h include/nvexec.h include/nvpat.h include/nvproc.h
	$CC $CFLAGS -o lib/process.$O lib/process.c

lib/sched.$O: lib/sched.c include/nvbc.h include/nvvm.h include/nvexec.h include/nvproc.h include/nvsched.h
	$CC $CFLAGS -o lib/sched.$O lib/sched.c

lib/value.$O: lib/value.c include/nvbc.h include/nvvm.h
	$CC $CFLAGS -o lib/value.$O lib/value.c

lib/verify.$O: lib/verify.c include/nvbc.h
	$CC $CFLAGS -o lib/verify.$O lib/verify.c

lib/vm.$O: lib/vm.c include/nvbc.h include/nvvm.h include/nvexec.h
	$CC $CFLAGS -o lib/vm.$O lib/vm.c

clean:V:
	rm -f cmd/nervous/*.[$OS] lib/*.[$OS] tests/pattern/*.[$OS] tests/pattern/ptest tests/pattern/parsetest tests/pattern/bctest tests/pattern/compiletest tests/process/ptest tests/process/exectest tests/process/schedtest tests/process/iotest tests/process/r2test tests/memory/*.[$OS] tests/memory/gctest tests/memory/autotest bench/perftest.$O bench/perftest $TARG

install:V: $TARG
	cp $TARG $BIN/
