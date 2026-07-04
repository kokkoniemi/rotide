// double-slash comment
/* block comment */
locals {
  enabled = true
  region  = "us-east-1"
  ports   = [80, 443]
  tags    = {
    env  = "prod"
    team = "core"
  }
}

resource "aws_instance" "web" {
  ami           = var.ami_id
  instance_type = "t2.micro"
  count         = local.enabled ? 2 : 0

  connection {
    host = self.public_ip
  }
}

output "endpoint" {
  value = format("https://%s", aws_instance.web.public_dns)
}
