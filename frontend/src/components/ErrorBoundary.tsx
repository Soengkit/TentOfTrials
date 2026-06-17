import React, { Component, ErrorInfo, ReactNode } from 'react';
import { telemetry } from '../services/telemetry';

interface ErrorBoundaryProps {
  children: ReactNode;
  fallback?: ReactNode | ((error: Error, retry: () => void) => ReactNode);
  onError?: (error: Error, errorInfo: ErrorInfo) => void;
}

interface ErrorBoundaryState {
  hasError: boolean;
  error: Error | null;
  errorInfo: ErrorInfo | null;
}

export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  constructor(props: ErrorBoundaryProps) {
    super(props);
    this.state = {
      hasError: false,
      error: null,
      errorInfo: null,
    };
  }

  static getDerivedStateFromError(error: Error): Partial<ErrorBoundaryState> {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo): void {
    this.setState({ errorInfo });
    console.error('ErrorBoundary caught an error:', error, errorInfo);
    telemetry.trackError(error, 'ErrorBoundary', ['error_boundary']);

    if (this.props.onError) {
      this.props.onError(error, errorInfo);
    }
  }

  handleRetry = (): void => {
    this.setState({ hasError: false, error: null, errorInfo: null });
  };

  handleCopyError = (): void => {
    const { error, errorInfo } = this.state;
    const details = [
      `Error: ${error?.message || 'Unknown'}`,
      `Stack: ${error?.stack || 'N/A'}`,
      `Component Stack: ${errorInfo?.componentStack || 'N/A'}`,
    ].join('\n\n');

    navigator.clipboard.writeText(details).catch((err) => {
      console.error('Failed to copy error details:', err);
    });
  };

  render(): ReactNode {
    if (this.state.hasError) {
      const { error } = this.state;

      if (this.props.fallback) {
        if (typeof this.props.fallback === 'function') {
          return (this.props.fallback as (error: Error, retry: () => void) => ReactNode)(error!, this.handleRetry);
        }
        return this.props.fallback;
      }

      try {
        return (
          <div style={{
            padding: '24px',
            margin: '16px',
            border: '1px solid #ef4444',
            borderRadius: '8px',
            backgroundColor: '#fef2f2',
            color: '#991b1b',
            fontFamily: 'system-ui, -apple-system, sans-serif',
          }}>
            <h2 style={{ margin: '0 0 12px', fontSize: '18px', fontWeight: 600 }}>
              Something went wrong
            </h2>
            <p style={{ margin: '0 0 8px', fontSize: '14px' }}>
              {error?.message || 'An unexpected error occurred'}
            </p>
            <div style={{ display: 'flex', gap: '8px' }}>
              <button
                onClick={this.handleRetry}
                style={{
                  padding: '8px 16px',
                  backgroundColor: '#ef4444',
                  color: 'white',
                  border: 'none',
                  borderRadius: '6px',
                  cursor: 'pointer',
                  fontSize: '14px',
                }}
              >
                Try Again
              </button>
              <button
                onClick={this.handleCopyError}
                style={{
                  padding: '8px 16px',
                  backgroundColor: 'transparent',
                  color: '#ef4444',
                  border: '1px solid #ef4444',
                  borderRadius: '6px',
                  cursor: 'pointer',
                  fontSize: '14px',
                }}
              >
                Copy Error Details
              </button>
            </div>
          </div>
        );
      } catch {
        return (
          <div style={{ padding: '16px', color: '#991b1b' }}>
            Something went very wrong
          </div>
        );
      }
    }

    return this.props.children;
  }
}
