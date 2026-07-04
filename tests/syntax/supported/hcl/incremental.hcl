variable "environment" {
  type    = string
  default = "dev"
}

resource "aws_instance" "db" {
  ami   = var.ami
  count = 2
}
