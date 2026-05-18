variable "vpc_id" { type = string }
variable "private_subnets" { type = list(string) }
variable "public_subnets" { type = list(string) }
variable "redis_endpoint" { type = string }

resource "aws_ecs_cluster" "search_cluster" {
  name = "multimodal-search-cluster"
}

# --- TASK DEFINITION: NATIVE C++ COMPUTE CORE ---
resource "aws_ecs_task_definition" "core_engine_task" {
  family                   = "core-engine-task"
  network_mode             = "awsvpc"
  requires_compatibilities = ["FARGATE"]
  cpu                      = "1024" # 1 vCPU Aligned to Thread Pools
  memory                   = "2048" # 2GB RAM

  container_definitions = jsonencode([
    {
      name      = "core-engine"
      image     = "YOUR_AWS_ACCOUNT_ID.dkr.ecr.us-east-1.amazonaws.com/core-engine:latest"
      essential = true
      portMappings = [
        {
          containerPort = 50051
          hostPort      = 50051
        }
      ]
    }
  ])
}

# --- SERVICE PROVISIONER ---
resource "aws_ecs_service" "core_engine_service" {
  name            = "core-engine-service"
  cluster         = aws_ecs_cluster.search_cluster.id
  task_definition = aws_ecs_task_definition.core_engine_task.arn
  desired_count   = 2
  launch_type     = "FARGATE"

  network_configuration {
    subnets          = var.private_subnets
    assign_public_ip = false
  }
}