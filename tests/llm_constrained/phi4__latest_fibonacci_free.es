{
  "program": {
    "functions": [
      {
        "name": "fib",
        "type": "recursive",
        "body": "n < 2 ? n : fib(n-1) + fib(n-2)"
      }
    ],
    "main": {
      "for_loop": {
        "index_variable": "i",
        "range": [1, 20],
        "body": [
          {
            "function_call": {
              "name": "print",
              "arguments": ["fib(i)"]
            }
          }
        ]
      }
    }
  }
}