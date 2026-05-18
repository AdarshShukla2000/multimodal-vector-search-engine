variable "vpc_id" { type = string }
variable "private_subnets" { type = list(string) }
variable "security_group_id" { type = string }

resource "aws_elasticache_subnet_group" "redis_subnets" {
  name       = "redis-cache-subnet-group"
  subnet_ids = var.private_subnets
}

resource "aws_elasticache_cluster" "redis" {
  cluster_id           = "hnsw-cache-cluster"
  engine               = "redis"
  node_type            = "cache.t4g.medium"
  num_cache_nodes      = 1
  parameter_group_name = "default.redis7"
  subnet_group_name    = aws_elasticache_subnet_group.redis_subnets.name
  security_group_ids   = [var.security_group_id]
}

output "redis_endpoint" {
  value = aws_elasticache_cluster.redis.cache_nodes[0].address
}