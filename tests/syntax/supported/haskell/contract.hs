{-# LANGUAGE QuasiQuotes #-}
module Demo where

import qualified Data.Text as Text

data User = User { userName :: String, userAge :: Int }
newtype UserId = UserId Int
type Label = String

class Render a where
  render :: a -> String

instance Render User where
  render user =
    let name = userName user
        active = True
    in if active then name else "inactive"

greet :: User -> String
greet user =
  case user of
    User name age -> name ++ show age

page = [hamlet|<p class="name">#{greet sample}</p>|]
style = [lucius|.name { color: blue; }|]
script = [julius|const name = "Ada";|]
payload = [aesonQQ|{"name": "Ada", "age": 42}|]

answer :: Int
answer = 42

ratio :: Double
ratio = 3.14

letter :: Char
letter = 'x'
