# Highlighting sampler
library(stats)

PI <- 3.14159
count <- 42L
z <- 1 + 2i
name <- "world"

area <- function(r) {
  pi * r^2
}

classify <- function(n) {
  if (n < 0) {
    "negative"
  } else if (n == 0) {
    "zero"
  } else {
    "positive"
  }
}

for (i in 1:3) {
  message(paste("item", i))
}

flags <- c(TRUE, FALSE, NA, NULL, Inf, NaN)
result <- stats::median(c(1, 2, 3))
piped <- c(1, 2, 3) |> sum()
