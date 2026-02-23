{
  "main_body": [
    {
      "type": "variable_declaration",
      "name": "acc",
      "value": 0
    },
    {
      "type": "for_loop",
      "start": 1,
      "end": 100,
      "body": [
        {
          "type": "expression",
          "operator": "+=",
          "left": "acc",
          "right": {
            "type": "multiplication",
            "left": {
              "type": "variable_reference",
              "name": "i"
            },
            "right": {
              "type": "number",
              "value": 2
            }
          }
        }
      ]
    },
    {
      "type": "print_statement",
      "expression": "acc"
    }
  ],
  "functions": [],
  "constants": []
}