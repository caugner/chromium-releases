// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/linux/seccomp-bpf/codegen.h"
#include "sandbox/linux/seccomp-bpf/sandbox_bpf.h"
#include "sandbox/linux/seccomp-bpf/syscall_iterator.h"
#include "sandbox/linux/seccomp-bpf/verifier.h"

namespace {

void WriteFailedStderrSetupMessage(int out_fd) {
  const char* error_string = strerror(errno);
  static const char msg[] = "Failed to set up stderr: ";
  if (HANDLE_EINTR(write(out_fd, msg, sizeof(msg)-1)) > 0 && error_string &&
      HANDLE_EINTR(write(out_fd, error_string, strlen(error_string))) > 0 &&
      HANDLE_EINTR(write(out_fd, "\n", 1))) {
  }
}

}  // namespace

// The kernel gives us a sandbox, we turn it into a playground :-)
// This is version 2 of the playground; version 1 was built on top of
// pre-BPF seccomp mode.
namespace playground2 {

const int kExpectedExitCode = 100;

// We define a really simple sandbox policy. It is just good enough for us
// to tell that the sandbox has actually been activated.
ErrorCode Sandbox::probeEvaluator(int signo) {
  switch (signo) {
  case __NR_getpid:
    // Return EPERM so that we can check that the filter actually ran.
    return ErrorCode(EPERM);
  case __NR_exit_group:
    // Allow exit() with a non-default return code.
    return ErrorCode(ErrorCode::ERR_ALLOWED);
  default:
    // Make everything else fail in an easily recognizable way.
    return ErrorCode(EINVAL);
  }
}

void Sandbox::probeProcess(void) {
  if (syscall(__NR_getpid) < 0 && errno == EPERM) {
    syscall(__NR_exit_group, static_cast<intptr_t>(kExpectedExitCode));
  }
}

bool Sandbox::isValidSyscallNumber(int sysnum) {
  return SyscallIterator::IsValid(sysnum);
}

ErrorCode Sandbox::allowAllEvaluator(int sysnum) {
  if (!isValidSyscallNumber(sysnum)) {
    return ErrorCode(ENOSYS);
  }
  return ErrorCode(ErrorCode::ERR_ALLOWED);
}

void Sandbox::tryVsyscallProcess(void) {
  time_t current_time;
  // time() is implemented as a vsyscall. With an older glibc, with
  // vsyscall=emulate and some versions of the seccomp BPF patch
  // we may get SIGKILL-ed. Detect this!
  if (time(&current_time) != static_cast<time_t>(-1)) {
    syscall(__NR_exit_group, static_cast<intptr_t>(kExpectedExitCode));
  }
}

bool Sandbox::RunFunctionInPolicy(void (*CodeInSandbox)(),
                                  EvaluateSyscall syscallEvaluator,
                                  int proc_fd) {
  // Block all signals before forking a child process. This prevents an
  // attacker from manipulating our test by sending us an unexpected signal.
  sigset_t oldMask, newMask;
  if (sigfillset(&newMask) ||
      sigprocmask(SIG_BLOCK, &newMask, &oldMask)) {
    SANDBOX_DIE("sigprocmask() failed");
  }
  int fds[2];
  if (pipe2(fds, O_NONBLOCK|O_CLOEXEC)) {
    SANDBOX_DIE("pipe() failed");
  }

  if (fds[0] <= 2 || fds[1] <= 2) {
    SANDBOX_DIE("Process started without standard file descriptors");
  }

  pid_t pid = fork();
  if (pid < 0) {
    // Die if we cannot fork(). We would probably fail a little later
    // anyway, as the machine is likely very close to running out of
    // memory.
    // But what we don't want to do is return "false", as a crafty
    // attacker might cause fork() to fail at will and could trick us
    // into running without a sandbox.
    sigprocmask(SIG_SETMASK, &oldMask, NULL);  // OK, if it fails
    SANDBOX_DIE("fork() failed unexpectedly");
  }

  // In the child process
  if (!pid) {
    // Test a very simple sandbox policy to verify that we can
    // successfully turn on sandboxing.
    Die::EnableSimpleExit();

    if (HANDLE_EINTR(close(fds[0]))) {
      WriteFailedStderrSetupMessage(fds[1]);
      SANDBOX_DIE(NULL);
    }
    if (HANDLE_EINTR(dup2(fds[1], 2)) != 2) {
      // Stderr could very well be a file descriptor to .xsession-errors, or
      // another file, which could be backed by a file system that could cause
      // dup2 to fail while trying to close stderr. It's important that we do
      // not fail on trying to close stderr.
      // If dup2 fails here, we will continue normally, this means that our
      // parent won't cause a fatal failure if something writes to stderr in
      // this child.
    }
    if (HANDLE_EINTR(close(fds[1]))) {
      WriteFailedStderrSetupMessage(fds[1]);
      SANDBOX_DIE(NULL);
    }

    evaluators_.clear();
    setSandboxPolicy(syscallEvaluator, NULL);
    setProcFd(proc_fd);

    // By passing "quiet=true" to "startSandboxInternal()" we suppress
    // messages for expected and benign failures (e.g. if the current
    // kernel lacks support for BPF filters).
    startSandboxInternal(true);

    // Run our code in the sandbox.
    CodeInSandbox();

    // CodeInSandbox() is not supposed to return here.
    SANDBOX_DIE(NULL);
  }

  // In the parent process.
  if (HANDLE_EINTR(close(fds[1]))) {
    SANDBOX_DIE("close() failed");
  }
  if (sigprocmask(SIG_SETMASK, &oldMask, NULL)) {
    SANDBOX_DIE("sigprocmask() failed");
  }
  int status;
  if (HANDLE_EINTR(waitpid(pid, &status, 0)) != pid) {
    SANDBOX_DIE("waitpid() failed unexpectedly");
  }
  bool rc = WIFEXITED(status) && WEXITSTATUS(status) == kExpectedExitCode;

  // If we fail to support sandboxing, there might be an additional
  // error message. If so, this was an entirely unexpected and fatal
  // failure. We should report the failure and somebody must fix
  // things. This is probably a security-critical bug in the sandboxing
  // code.
  if (!rc) {
    char buf[4096];
    ssize_t len = HANDLE_EINTR(read(fds[0], buf, sizeof(buf) - 1));
    if (len > 0) {
      while (len > 1 && buf[len-1] == '\n') {
        --len;
      }
      buf[len] = '\000';
      SANDBOX_DIE(buf);
    }
  }
  if (HANDLE_EINTR(close(fds[0]))) {
    SANDBOX_DIE("close() failed");
  }

  return rc;
}

bool Sandbox::kernelSupportSeccompBPF(int proc_fd) {
#if defined(SECCOMP_BPF_VALGRIND_HACKS)
  if (RUNNING_ON_VALGRIND) {
    // Valgrind doesn't like our run-time test. Disable testing and assume we
    // always support sandboxing. This feature should only ever be enabled when
    // debugging.
    return true;
  }
#endif

  return RunFunctionInPolicy(probeProcess, Sandbox::probeEvaluator, proc_fd) &&
         RunFunctionInPolicy(tryVsyscallProcess, Sandbox::allowAllEvaluator,
                             proc_fd);
}

Sandbox::SandboxStatus Sandbox::supportsSeccompSandbox(int proc_fd) {
  // It the sandbox is currently active, we clearly must have support for
  // sandboxing.
  if (status_ == STATUS_ENABLED) {
    return status_;
  }

  // Even if the sandbox was previously available, something might have
  // changed in our run-time environment. Check one more time.
  if (status_ == STATUS_AVAILABLE) {
    if (!isSingleThreaded(proc_fd)) {
      status_ = STATUS_UNAVAILABLE;
    }
    return status_;
  }

  if (status_ == STATUS_UNAVAILABLE && isSingleThreaded(proc_fd)) {
    // All state transitions resulting in STATUS_UNAVAILABLE are immediately
    // preceded by STATUS_AVAILABLE. Furthermore, these transitions all
    // happen, if and only if they are triggered by the process being multi-
    // threaded.
    // In other words, if a single-threaded process is currently in the
    // STATUS_UNAVAILABLE state, it is safe to assume that sandboxing is
    // actually available.
    status_ = STATUS_AVAILABLE;
    return status_;
  }

  // If we have not previously checked for availability of the sandbox or if
  // we otherwise don't believe to have a good cached value, we have to
  // perform a thorough check now.
  if (status_ == STATUS_UNKNOWN) {
    status_ = kernelSupportSeccompBPF(proc_fd)
      ? STATUS_AVAILABLE : STATUS_UNSUPPORTED;

    // As we are performing our tests from a child process, the run-time
    // environment that is visible to the sandbox is always guaranteed to be
    // single-threaded. Let's check here whether the caller is single-
    // threaded. Otherwise, we mark the sandbox as temporarily unavailable.
    if (status_ == STATUS_AVAILABLE && !isSingleThreaded(proc_fd)) {
      status_ = STATUS_UNAVAILABLE;
    }
  }
  return status_;
}

void Sandbox::setProcFd(int proc_fd) {
  proc_fd_ = proc_fd;
}

void Sandbox::startSandboxInternal(bool quiet) {
  if (status_ == STATUS_UNSUPPORTED || status_ == STATUS_UNAVAILABLE) {
    SANDBOX_DIE("Trying to start sandbox, even though it is known to be "
                "unavailable");
  } else if (status_ == STATUS_ENABLED) {
    SANDBOX_DIE("Cannot start sandbox recursively. Use multiple calls to "
                "setSandboxPolicy() to stack policies instead");
  }
  if (proc_fd_ < 0) {
    proc_fd_ = open("/proc", O_RDONLY|O_DIRECTORY);
  }
  if (proc_fd_ < 0) {
    // For now, continue in degraded mode, if we can't access /proc.
    // In the future, we might want to tighten this requirement.
  }
  if (!isSingleThreaded(proc_fd_)) {
    SANDBOX_DIE("Cannot start sandbox, if process is already multi-threaded");
  }

  // We no longer need access to any files in /proc. We want to do this
  // before installing the filters, just in case that our policy denies
  // close().
  if (proc_fd_ >= 0) {
    if (HANDLE_EINTR(close(proc_fd_))) {
      SANDBOX_DIE("Failed to close file descriptor for /proc");
    }
    proc_fd_ = -1;
  }

  // Install the filters.
  installFilter(quiet);

  // We are now inside the sandbox.
  status_ = STATUS_ENABLED;
}

bool Sandbox::isSingleThreaded(int proc_fd) {
  if (proc_fd < 0) {
    // Cannot determine whether program is single-threaded. Hope for
    // the best...
    return true;
  }

  struct stat sb;
  int task = -1;
  if ((task = openat(proc_fd, "self/task", O_RDONLY|O_DIRECTORY)) < 0 ||
      fstat(task, &sb) != 0 ||
      sb.st_nlink != 3 ||
      HANDLE_EINTR(close(task))) {
    if (task >= 0) {
      if (HANDLE_EINTR(close(task))) { }
    }
    return false;
  }
  return true;
}

bool Sandbox::isDenied(const ErrorCode& code) {
  return (code.err() & SECCOMP_RET_ACTION) == SECCOMP_RET_TRAP ||
         (code.err() >= (SECCOMP_RET_ERRNO + ErrorCode::ERR_MIN_ERRNO) &&
          code.err() <= (SECCOMP_RET_ERRNO + ErrorCode::ERR_MAX_ERRNO));
}

void Sandbox::policySanityChecks(EvaluateSyscall syscallEvaluator,
                                 EvaluateArguments) {
  for (SyscallIterator iter(true); !iter.Done(); ) {
    uint32_t sysnum = iter.Next();
    if (!isDenied(syscallEvaluator(sysnum))) {
      SANDBOX_DIE("Policies should deny system calls that are outside the "
                  "expected range (typically MIN_SYSCALL..MAX_SYSCALL)");
    }
  }
  return;
}

void Sandbox::setSandboxPolicy(EvaluateSyscall syscallEvaluator,
                               EvaluateArguments argumentEvaluator) {
  if (status_ == STATUS_ENABLED) {
    SANDBOX_DIE("Cannot change policy after sandbox has started");
  }
  policySanityChecks(syscallEvaluator, argumentEvaluator);
  evaluators_.push_back(std::make_pair(syscallEvaluator, argumentEvaluator));
}

void Sandbox::installFilter(bool quiet) {
  // Verify that the user pushed a policy.
  if (evaluators_.empty()) {
  filter_failed:
    SANDBOX_DIE("Failed to configure system call filters");
  }

  // Set new SIGSYS handler
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = &sigSys;
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGSYS, &sa, NULL) < 0) {
    goto filter_failed;
  }

  // Unmask SIGSYS
  sigset_t mask;
  if (sigemptyset(&mask) ||
      sigaddset(&mask, SIGSYS) ||
      sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
    goto filter_failed;
  }

  // We can't handle stacked evaluators, yet. We'll get there eventually
  // though. Hang tight.
  if (evaluators_.size() != 1) {
    SANDBOX_DIE("Not implemented");
  }

  // Assemble the BPF filter program.
  CodeGen *gen = new CodeGen();
  if (!gen) {
    SANDBOX_DIE("Out of memory");
  }

  // If the architecture doesn't match SECCOMP_ARCH, disallow the
  // system call.
  Instruction *tail;
  Instruction *head =
    gen->MakeInstruction(BPF_LD+BPF_W+BPF_ABS,
                         offsetof(struct arch_seccomp_data, arch),
    gen->MakeInstruction(BPF_JMP+BPF_JEQ+BPF_K, SECCOMP_ARCH,
  tail =
    // Grab the system call number, so that we can implement jump tables.
    gen->MakeInstruction(BPF_LD+BPF_W+BPF_ABS,
                         offsetof(struct arch_seccomp_data, nr)),
    gen->MakeInstruction(BPF_RET+BPF_K,
                         Kill(
                           "Invalid audit architecture in BPF filter").err_)));

  // On Intel architectures, verify that system call numbers are in the
  // expected number range. The older i386 and x86-64 APIs clear bit 30
  // on all system calls. The newer x32 API always sets bit 30.
#if defined(__i386__) || defined(__x86_64__)
  Instruction *invalidX32 =
    gen->MakeInstruction(BPF_RET+BPF_K,
                         Kill("Illegal mixing of system call ABIs").err_);
  Instruction *checkX32 =
#if defined(__x86_64__) && defined(__ILP32__)
    gen->MakeInstruction(BPF_JMP+BPF_JSET+BPF_K, 0x40000000, 0, invalidX32);
#else
    gen->MakeInstruction(BPF_JMP+BPF_JSET+BPF_K, 0x40000000, invalidX32, 0);
#endif
    gen->JoinInstructions(tail, checkX32);
    tail = checkX32;
#endif


  {
    // Evaluate all possible system calls and group their ErrorCodes into
    // ranges of identical codes.
    Ranges ranges;
    findRanges(&ranges);

    // Compile the system call ranges to an optimized BPF jumptable
    Instruction *jumptable =
      assembleJumpTable(gen, ranges.begin(), ranges.end());

    // Append jump table to our pre-amble
    gen->JoinInstructions(tail, jumptable);
  }

  // Turn the DAG into a vector of instructions.
  Program *program = new Program();
  gen->Compile(head, program);
  delete gen;

  // Make sure compilation resulted in BPF program that executes
  // correctly. Otherwise, there is an internal error in our BPF compiler.
  // There is really nothing the caller can do until the bug is fixed.
#ifndef NDEBUG
  const char *err = NULL;
  if (!Verifier::VerifyBPF(*program, evaluators_, &err)) {
    SANDBOX_DIE(err);
  }
#endif

  // We want to be very careful in not imposing any requirements on the
  // policies that are set with setSandboxPolicy(). This means, as soon as
  // the sandbox is active, we shouldn't be relying on libraries that could
  // be making system calls. This, for example, means we should avoid
  // using the heap and we should avoid using STL functions.
  // Temporarily copy the contents of the "program" vector into a
  // stack-allocated array; and then explicitly destroy that object.
  // This makes sure we don't ex- or implicitly call new/delete after we
  // installed the BPF filter program in the kernel. Depending on the
  // system memory allocator that is in effect, these operators can result
  // in system calls to things like munmap() or brk().
  struct sock_filter bpf[program->size()];
  const struct sock_fprog prog = {
    static_cast<unsigned short>(program->size()), bpf };
  memcpy(bpf, &(*program)[0], sizeof(bpf));
  delete program;

  // Release memory that is no longer needed
  evaluators_.clear();
  errMap_.clear();

#if defined(SECCOMP_BPF_VALGRIND_HACKS)
  // Valgrind is really not happy about our sandbox. Disable it when running
  // in Valgrind. This feature is dangerous and should never be enabled by
  // default. We protect it behind a pre-processor option.
  if (!RUNNING_ON_VALGRIND)
#endif
  {
    // Install BPF filter program
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
      SANDBOX_DIE(quiet ? NULL : "Kernel refuses to enable no-new-privs");
    } else {
      if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) {
        SANDBOX_DIE(quiet ? NULL : "Kernel refuses to turn on BPF filters");
      }
    }
  }

  return;
}

void Sandbox::findRanges(Ranges *ranges) {
  // Please note that "struct seccomp_data" defines system calls as a signed
  // int32_t, but BPF instructions always operate on unsigned quantities. We
  // deal with this disparity by enumerating from MIN_SYSCALL to MAX_SYSCALL,
  // and then verifying that the rest of the number range (both positive and
  // negative) all return the same ErrorCode.
  EvaluateSyscall evaluateSyscall = evaluators_.begin()->first;
  uint32_t oldSysnum              = 0;
  ErrorCode oldErr                = evaluateSyscall(oldSysnum);
  ErrorCode invalidErr            = evaluateSyscall(MIN_SYSCALL - 1);
  for (SyscallIterator iter(false); !iter.Done(); ) {
    uint32_t sysnum = iter.Next();
    ErrorCode err = evaluateSyscall(static_cast<int>(sysnum));
    if (!iter.IsValid(sysnum) && !invalidErr.Equals(err)) {
      // A proper sandbox policy should always treat system calls outside of
      // the range MIN_SYSCALL..MAX_SYSCALL (i.e. anything that returns
      // "false" for SyscallIterator::IsValid()) identically. Typically, all
      // of these system calls would be denied with the same ErrorCode.
      SANDBOX_DIE("Invalid seccomp policy");
    }
    if (!err.Equals(oldErr) || iter.Done()) {
      ranges->push_back(Range(oldSysnum, sysnum - 1, oldErr));
      oldSysnum = sysnum;
      oldErr    = err;
    }
  }
}

Instruction *Sandbox::assembleJumpTable(CodeGen *gen,
                                        Ranges::const_iterator start,
                                        Ranges::const_iterator stop) {
  // We convert the list of system call ranges into jump table that performs
  // a binary search over the ranges.
  // As a sanity check, we need to have at least one distinct ranges for us
  // to be able to build a jump table.
  if (stop - start <= 0) {
    SANDBOX_DIE("Invalid set of system call ranges");
  } else if (stop - start == 1) {
    // If we have narrowed things down to a single range object, we can
    // return from the BPF filter program.
    return gen->MakeInstruction(BPF_RET+BPF_K, start->err);
  }

  // Pick the range object that is located at the mid point of our list.
  // We compare our system call number against the lowest valid system call
  // number in this range object. If our number is lower, it is outside of
  // this range object. If it is greater or equal, it might be inside.
  Ranges::const_iterator mid = start + (stop - start)/2;

  // Sub-divide the list of ranges and continue recursively.
  Instruction *jf = assembleJumpTable(gen, start, mid);
  Instruction *jt = assembleJumpTable(gen, mid, stop);
  return gen->MakeInstruction(BPF_JMP+BPF_JGE+BPF_K, mid->from, jt, jf);
}

void Sandbox::sigSys(int nr, siginfo_t *info, void *void_context) {
  // Various sanity checks to make sure we actually received a signal
  // triggered by a BPF filter. If something else triggered SIGSYS
  // (e.g. kill()), there is really nothing we can do with this signal.
  if (nr != SIGSYS || info->si_code != SYS_SECCOMP || !void_context ||
      info->si_errno <= 0 ||
      static_cast<size_t>(info->si_errno) > trapArraySize_) {
    // SANDBOX_DIE() can call LOG(FATAL). This is not normally async-signal
    // safe and can lead to bugs. We should eventually implement a different
    // logging and reporting mechanism that is safe to be called from
    // the sigSys() handler.
    // TODO: If we feel confident that our code otherwise works correctly, we
    //       could actually make an argument that spurious SIGSYS should
    //       just get silently ignored. TBD
  sigsys_err:
    SANDBOX_DIE("Unexpected SIGSYS received");
  }

  // Signal handlers should always preserve "errno". Otherwise, we could
  // trigger really subtle bugs.
  int old_errno   = errno;

  // Obtain the signal context. This, most notably, gives us access to
  // all CPU registers at the time of the signal.
  ucontext_t *ctx = reinterpret_cast<ucontext_t *>(void_context);

  // Obtain the siginfo information that is specific to SIGSYS. Unfortunately,
  // most versions of glibc don't include this information in siginfo_t. So,
  // we need to explicitly copy it into a arch_sigsys structure.
  struct arch_sigsys sigsys;
  memcpy(&sigsys, &info->_sifields, sizeof(sigsys));

  // Some more sanity checks.
  if (sigsys.ip != reinterpret_cast<void *>(SECCOMP_IP(ctx)) ||
      sigsys.nr != static_cast<int>(SECCOMP_SYSCALL(ctx)) ||
      sigsys.arch != SECCOMP_ARCH) {
    goto sigsys_err;
  }

  // Copy the seccomp-specific data into a arch_seccomp_data structure. This
  // is what we are showing to TrapFnc callbacks that the system call evaluator
  // registered with the sandbox.
  struct arch_seccomp_data data = {
    sigsys.nr,
    SECCOMP_ARCH,
    reinterpret_cast<uint64_t>(sigsys.ip),
    {
      static_cast<uint64_t>(SECCOMP_PARM1(ctx)),
      static_cast<uint64_t>(SECCOMP_PARM2(ctx)),
      static_cast<uint64_t>(SECCOMP_PARM3(ctx)),
      static_cast<uint64_t>(SECCOMP_PARM4(ctx)),
      static_cast<uint64_t>(SECCOMP_PARM5(ctx)),
      static_cast<uint64_t>(SECCOMP_PARM6(ctx))
    }
  };

  // Now call the TrapFnc callback associated with this particular instance
  // of SECCOMP_RET_TRAP.
  const ErrorCode& err = trapArray_[info->si_errno - 1];
  intptr_t rc          = err.fnc_(data, err.aux_);

  // Update the CPU register that stores the return code of the system call
  // that we just handled, and restore "errno" to the value that it had
  // before entering the signal handler.
  SECCOMP_RESULT(ctx) = static_cast<greg_t>(rc);
  errno               = old_errno;

  return;
}

ErrorCode Sandbox::Trap(ErrorCode::TrapFnc fnc, const void *aux) {
  // Each unique pair of TrapFnc and auxiliary data make up a distinct instance
  // of a SECCOMP_RET_TRAP.
  std::pair<ErrorCode::TrapFnc, const void *> key(fnc, aux);
  TrapIds::const_iterator iter = trapIds_.find(key);
  uint16_t id;
  if (iter != trapIds_.end()) {
    // We have seen this pair before. Return the same id that we assigned
    // earlier.
    id = iter->second;
  } else {
    // This is a new pair. Remember it and assign a new id.
    // Please note that we have to store traps in memory that doesn't get
    // deallocated when the program is shutting down. A memory leak is
    // intentional, because we might otherwise not be able to execute
    // system calls part way through the program shutting down
    if (!traps_) {
      traps_ = new Traps();
    }
    if (traps_->size() >= SECCOMP_RET_DATA) {
      // In practice, this is pretty much impossible to trigger, as there
      // are other kernel limitations that restrict overall BPF program sizes.
      SANDBOX_DIE("Too many SECCOMP_RET_TRAP callback instances");
    }
    id = traps_->size() + 1;

    traps_->push_back(ErrorCode(fnc, aux, id));
    trapIds_[key] = id;

    // We want to access the traps_ vector from our signal handler. But
    // we are not assured that doing so is async-signal safe. On the other
    // hand, C++ guarantees that the contents of a vector is stored in a
    // contiguous C-style array.
    // So, we look up the address and size of this array outside of the
    // signal handler, where we can safely do so.
    trapArray_     = &(*traps_)[0];
    trapArraySize_ = id;
  }

  ErrorCode err = ErrorCode(fnc, aux, id);
  return errMap_[err.err()] = err;
}

intptr_t Sandbox::bpfFailure(const struct arch_seccomp_data&, void *aux) {
  SANDBOX_DIE(static_cast<char *>(aux));
}

ErrorCode Sandbox::Kill(const char *msg) {
  return Trap(bpfFailure, const_cast<char *>(msg));
}

Sandbox::SandboxStatus Sandbox::status_ = STATUS_UNKNOWN;
int    Sandbox::proc_fd_                = -1;
Sandbox::Evaluators Sandbox::evaluators_;
Sandbox::ErrMap Sandbox::errMap_;
Sandbox::Traps *Sandbox::traps_         = NULL;
Sandbox::TrapIds Sandbox::trapIds_;
ErrorCode *Sandbox::trapArray_          = NULL;
size_t Sandbox::trapArraySize_          = 0;

}  // namespace
