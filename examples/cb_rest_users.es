use rest
listen 8080
model user name email
GET /users list user
POST /users create user
GET /health "ok"
