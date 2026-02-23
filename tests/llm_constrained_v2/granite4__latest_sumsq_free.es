{
  "main_body": [
    {
      "type": "variable",
      "name": "acc",
      "value": "0"
    },
    {
      "type": "for_loop",
      "start": "1",
      "end": "100",
      "body": [
        {
          "type": "assignment",
          "target": "acc",
          "expression": "{ acc += i * i }"
        }
      ]
    },
    {
      "type": "print",
      "value": "expr"
    }
  ],
  "functions": [],
  "expressions": [
    {
      "name": "sum",
      "parameters": ["range"],
      "body": null
    }
  ]
}