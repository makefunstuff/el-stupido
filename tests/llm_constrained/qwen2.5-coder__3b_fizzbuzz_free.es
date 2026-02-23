{
    "main_body": [
        {
            "type": "for",
            "start": 1,
            "end": 30,
            "body": [
                {
                    "type": "if",
                    "expr": [
                        {
                            "type": "mod",
                            "left": {
                                "type": "var",
                                "name": "i"
                            },
                            "right": 15
                        }
                    ],
                    "then": [
                        {
                            "type": "printf",
                            "args": ["FizzBuzz"]
                        }
                    ]
                },
                {
                    "type": "if",
                    "expr": [
                        {
                            "type": "mod",
                            "left": {
                                "type": "var",
                                "name": "i"
                            },
                            "right": 3
                        }
                    ],
                    "then": [
                        {
                            "type": "printf",
                            "args": ["Fizz"]
                        }
                    ]
                },
                {
                    "type": "if",
                    "expr": [
                        {
                            "type": "mod",
                            "left": {
                                "type": "var",
                                "name": "i"
                            },
                            "right": 5
                        }
                    ],
                    "then": [
                        {
                            "type": "printf",
                            "args": ["Buzz"]
                        }
                    ]
                },
                {
                    "type": "else",
                    "body": [
                        {
                            "type": "printf",
                            "args": [
                                {
                                    "type": "var",
                                    "name": "i"
                                }
                            ]
                        }
                    ]
                }
            ]
        }
    ]
}