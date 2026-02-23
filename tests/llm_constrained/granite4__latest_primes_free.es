{
  "program": [
    {
      "type": "variable",
      "name": "i",
      "init": "1"
    },
    {
      "type": "while",
      "condition": "i <= 50",
      "body": [
        {
          "type": "variable",
          "name": "j",
          "init": "2"
        },
        {
          "type": "while",
          "condition": "j < i",
          "body": [
            {
              "type": "assignment",
              "left": "j",
              "right": "j + 1"
            }
          ]
        },
        {
          "type": "if",
          "cond": "i % j == 0",
          "then": [],
          "else": [
            {
              "type": "print",
              "expr": "i"
            },
            {
              "type": "break"
            }
          ]
        }
      ]
    }
  ]
}