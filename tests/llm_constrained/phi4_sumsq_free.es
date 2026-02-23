-stupido
{
  "main_body": [
    {
      "type": "assignment",
      "target": "acc",
      "value": 0
    },
    {
      "type": "for_loop",
      "variable": "i",
      "range": {
        "start": 1,
        "end": 100,
        "inclusive": true
      },
      "body": [
        {
          "type": "assignment",
          "target": "acc",
          "operation": "+=",
          "value": {
            "type": "expression",
            "operator": "*",
            "operands": ["i", "i"]
          }
        }
      ]
    },
    {
      "type": "print",
      "expr": "acc"
    }
  ]
}