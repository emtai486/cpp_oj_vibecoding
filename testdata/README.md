# Test Data

Problem test data is stored outside PostgreSQL and referenced by database metadata.

Expected layout:

```txt
testdata/
  problems/
    {problem_id}/
      samples/
      public/
      hidden/
```

Hidden input and output files must not be exposed to users or AI analysis prompts.

