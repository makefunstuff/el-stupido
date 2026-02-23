{
  "main_body": [
    {
      "type": "variable",
      "name": "acc",
      "value": 0
    },
    {
      "type": "for_loop",
      "start": 1,
      "end": 100,
      "body": [
        {
          "type": "assignment",
          "left": "acc",
          "right": [
            {
              "type": "binary_operation",
              "operator": "+",
              "left": "$.acc",
              "right": [
                {
                  "type": "variable",
                  "name": "i"
                },
                {
                  "type": "variable",
                  "name": "i"
                }
              ]
            }
          ]
        }
      ]
    },
    {
      "type": "print",
      "value": "$.acc"
    }
  ],
  "functions": []
}