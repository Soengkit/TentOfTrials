module System.Random
  ( randomRIO
  ) where

randomRIO :: (a, a) -> IO a
randomRIO (lo, _) = pure lo
