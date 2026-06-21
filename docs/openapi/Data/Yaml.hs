module Data.Yaml
  ( ParseException(..)
  , decodeFileEither
  ) where

import Data.Aeson (FromJSON, Value(Null), parseJSON)

newtype ParseException = ParseException String
  deriving (Show, Eq)

decodeFileEither :: FromJSON a => FilePath -> IO (Either ParseException a)
decodeFileEither _ =
  pure $
    case parseJSON Null of
      Left err -> Left (ParseException err)
      Right value -> Right value
