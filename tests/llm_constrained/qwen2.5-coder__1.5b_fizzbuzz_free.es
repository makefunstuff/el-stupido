{
  "main_body": {
    "for": [
      {
        "init": "i",
        "condition": "i <= 30",
        "increment": "i + 1"
      }
    ],
    "if": [
      {
        "condition": "i % 15 == 0",
        "body": "printf(\"FizzBuzz\\n\", i)"
      },
      {
        "condition": "i % 3 == 0",
        "body": "printf(\"Fizz\\n\", i)"
      },
      {
        "condition": "i % 5 == 0",
        "body": "printf(\"Buzz\\n\", i)"
      }
    ],
    "else": [
      {
        "body": "printf(\"%d\\n\", i)"
      }
    ]
  }
}