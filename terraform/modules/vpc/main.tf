resource "aws_vpc" "main" {
  cidr_block           = "10.0.0.0/16"
  enable_dns_hostnames = true
  tags = { Name = "multimodal-search-vpc" }
}

resource "aws_subnet" "public_1" {
  vpc_id            = aws_vpc.main.id
  cidr_block        = "10.0.1.0/24"
  availability_zone = "us-east-1a"
}

resource "aws_subnet" "private_1" {
  vpc_id            = aws_vpc.main.id
  cidr_block        = "10.0.10.0/24"
  availability_zone = "us-east-1a"
}

resource "aws_security_group" "redis_sg" {
  name        = "redis-security-group"
  vpc_id      = aws_vpc.main.id
  ingress {
    from_port   = 6379
    to_port     = 6379
    protocol    = "tcp"
    cidr_blocks = ["10.0.0.0/16"]
  }
}

output "vpc_id" { value = aws_vpc.main.id }
output "public_subnets" { value = [aws_subnet.public_1.id] }
output "private_subnets" { value = [aws_subnet.private_1.id] }
output "cache_security_group_id" { value = aws_security_group.redis_sg.id }