terraform {
  required_version = ">= 1.5.0"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region = var.aws_region
}

# --- 1. CORE NETWORKING SUBSTRATE ---
module "vpc" {
  source = "./modules/vpc"
}

# --- 2. HIGH PERFORMANCE CACHING SUITE ---
module "elasticache" {
  source            = "./modules/elasticache"
  vpc_id            = module.vpc.vpc_id
  private_subnets   = module.vpc.private_subnets
  security_group_id = module.vpc.cache_security_group_id
}

# --- 3. CONTAINER COMPUTATION LAYER (ECS FARGATE) ---
module "ecs" {
  source             = "./modules/ecs"
  vpc_id             = module.vpc.vpc_id
  private_subnets    = module.vpc.private_subnets
  public_subnets     = module.vpc.public_subnets
  redis_endpoint     = module.elasticache.redis_endpoint
}

# --- 4. EVENT DRIVEN LAMBDA CONNECTOR ---
module "lambda" {
  source         = "./modules/lambda"
  vpc_id         = module.vpc.vpc_id
  private_subnet = module.vpc.private_subnets[0]
}