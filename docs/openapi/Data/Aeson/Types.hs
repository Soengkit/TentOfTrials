module Data.Aeson.Types
  ( Parser
  , parseMaybe
  ) where

type Parser = Either String

parseMaybe :: (a -> Parser b) -> a -> Maybe b
parseMaybe parser value =
  case parser value of
    Left _ -> Nothing
    Right parsed -> Just parsed
