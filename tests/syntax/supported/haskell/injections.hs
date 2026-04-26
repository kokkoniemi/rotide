{-# LANGUAGE QuasiQuotes #-}
module Main where
html = [hamlet|<section class="card">Hi</section>|]
css = [lucius|.card { color: red; }|]
js = [julius|const answer = 42;|]
ts = [tsc|const answer: number = 42;|]
json = [aesonQQ|{"answer": 42}|]
sql = [sql|SELECT 1|]
