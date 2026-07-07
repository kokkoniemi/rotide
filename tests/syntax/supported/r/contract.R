# Comprehensive R fixture exercising the highlight and locals queries.
library(methods)
suppressMessages(library(utils))

DEFAULTS <- list(min = 0L, max = 100L, label = "range")

#' Compute the euclidean norm of a numeric vector.
norm2 <- function(v) {
  sqrt(sum(v^2))
}

scale_values <- function(x, lower = 0, upper = 1) {
  rng <- range(x, na.rm = TRUE)
  if (diff(rng) == 0) {
    return(rep(lower, length(x)))
  }
  lower + (x - rng[1]) / diff(rng) * (upper - lower)
}

Point <- setRefClass("Point",
  fields = list(x = "numeric", y = "numeric"),
  methods = list(
    distance = function(other) {
      sqrt((x - other$x)^2 + (y - other$y)^2)
    }
  )
)

process <- function(items, ...) {
  items |>
    Filter(f = function(v) v %% 2 == 0) |>
    vapply(FUN = function(v) v * v, FUN.VALUE = numeric(1)) |>
    sum()
}

main <- function() {
  values <- c(1, 2, 3, 4, 5, NA)
  clean <- values[!is.na(values)]
  total <- 0
  i <- 1
  while (i <= length(clean)) {
    total <- total + clean[[i]]
    i <- i + 1
  }
  cat(sprintf("total=%d mean=%.2f\n", total, base::mean(clean)))
  repeat {
    if (total <= 0) break
    total <- total - 10
    if (total < 0) next
  }
  flags <- c(read = TRUE, write = FALSE)
  invisible(flags)
}

x = 10
20 -> y
z <<- x + y
main()
