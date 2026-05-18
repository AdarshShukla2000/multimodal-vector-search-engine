variable "vpc_id" { type = string }
variable "private_subnet" { type = string }

resource "aws_iam_role" "lambda_role" {
  name = "search-engine-lambda-role"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
    }]
  })
}

resource "aws_lambda_function" "ingest_trigger" {
  filename      = "lambda_function_payload.zip" # Dummy file target placeholder
  function_name = "vector-pipeline-event-trigger"
  role          = aws_iam_role.lambda_role.arn
  handler       = "index.handler"
  runtime       = "nodejs18.x"

  vpc_config {
    subnet_ids         = [var.private_subnet]
    security_group_ids = []
  }
}