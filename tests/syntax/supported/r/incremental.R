counter <- 0

increment <- function(by = 1) {
  counter <<- counter + by
  counter
}

values <- sapply(1:5, function(n) n * n)
total <- sum(values)
cat("total:", total, "\n")
