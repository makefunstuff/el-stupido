use rest
listen 9090
model project name desc owner
model task title status assignee priority
model comment author body
GET /api/projects list project
POST /api/projects create project
GET /api/tasks list task
POST /api/tasks create task
GET /api/comments list comment
POST /api/comments create comment
GET /health "{\"status\":\"ok\",\"version\":\"1.0\"}"
