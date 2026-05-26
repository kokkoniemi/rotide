# Filter misc-include-cleaner warnings for symbols whose canonical provider
# per clang-tidy's IWYU mapping is a glibc internal header (<bits/*>) rather
# than the public header we already include directly.  Adding <bits/*> headers
# would be strictly worse than the warning, so suppress these instead.
#
# Context lines (source excerpt + caret) are detected by their format and
# skipped together with the matched warning line.

/no header providing "(CLOCK_MONOTONIC|CLOCK_REALTIME|PATH_MAX|poll|pollfd|POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL|nfds_t|rusage|TIOCSWINSZ|TIOCGWINSZ|FIONREAD|errno|ETIMEDOUT|EACCES|EEXIST|EINTR|EIO|ENOENT|EROFS|siginfo_t|pthread_t|pthread_mutex_t|pthread_cond_t|mbstate_t|CHAR_MIN|CHAR_MAX|strdup|stat)" is directly included/ {
    skip = 1
    next
}

# Context lines: "  NNN | code" or "      |     ^~~~"
skip && /^[[:space:]]+[0-9]*[[:space:]]*\|/ { next }

# First non-context line resets skip
skip { skip = 0 }

# Drop per-file progress and warning-count lines
/^\[[0-9]+\/[0-9]+\] Processing file / { next }
/^[0-9]+ warning(s)? generated\./ { next }
/^Suppressed [0-9]+ warning/ { next }

{ print }
